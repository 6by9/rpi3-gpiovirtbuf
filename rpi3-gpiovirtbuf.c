/*
 * rpi3-gpiovirtbuf.c -- Control Raspberry Pi 3's activity LED by using the Mailbox interface
 *
 * Copyright (c) 2016 Sugizaki Yukimasa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include "raspberrypi-firmware.h"

#define RPI_FIRMWARE_DEV "/dev/vcio"
#define IOCTL_RPI_FIRMWARE_PROPERTY _IOWR(100, 0, char*)

#define DEV_MEM "/dev/mem"

#define BUS_TO_PHYS(addr) ((((addr)) & ~0xc0000000))

static void usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [s|g] NUM [STATE]\n", progname);
	fprintf(stderr, "       Set or get the GPIO state of GPIO NUM using the GPU mailbox service\n");
	fprintf(stderr, "     : %s c NUM DIR POLARITY TERM_EN PULL_UP STATE\n", progname);
	fprintf(stderr, "       Set the pin config of GPIO NUM.\n");
	fprintf(stderr, "       DIR: 0=in, 1=out.\n");
	fprintf(stderr, "       POLARITY: 0=active high, 1=active low (inverted).\n");
	fprintf(stderr, "       TERM_EN: 0=no termination, 1=termination enabled\n");
	fprintf(stderr, "       PULL_UP: 0=pull down, 1=pull up. Only if TERM_EN=1 and DIR=0\n");
	fprintf(stderr, "       STATE: state to set output to. Only if DIR=1\n");
}

static int rpi_firmware_open()
{
	int fd;

	fd = open(RPI_FIRMWARE_DEV, O_NONBLOCK);
	if (fd == -1) {
		fprintf(stderr, "error: open: %s: %s\n", RPI_FIRMWARE_DEV, strerror(errno));
		exit(EXIT_FAILURE);
	}

	return fd;
}

static void rpi_firmware_close(const int fd)
{
	int reti;

	reti = close(fd);
	if (reti == -1) {
		fprintf(stderr, "error: close: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static void rpi_firmware_property(const int fd, const uint32_t tag, void *tag_data, const size_t buf_size)
{
	int i;
	uint32_t *p = NULL;
	int reti;

	p = malloc((5 + buf_size / 4 + 1) * sizeof(*p));
	if (p == NULL) {
		fprintf(stderr, "error: Failed to allocate memory for RPi firmware\n");
		exit(EXIT_FAILURE);
	}

	i = 0;
	p[i++] = (5 + buf_size / 4 + 1) * sizeof(*p);
	p[i++] = RPI_FIRMWARE_STATUS_REQUEST;
	p[i++] = tag; // tag
	p[i++] = buf_size; // buf_size
	p[i++] = 0; // req_resp_size
	memcpy(p + i, tag_data, buf_size);
	p[i + buf_size / 4] = RPI_FIRMWARE_PROPERTY_END;;

	reti = ioctl(fd, IOCTL_RPI_FIRMWARE_PROPERTY, p);
	if (reti == -1) {
		fprintf(stderr, "error: ioctl: IOCTL_RPI_FIRMWARE_PROPERTY: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (p[1] != RPI_FIRMWARE_STATUS_SUCCESS) {
		fprintf(stderr, "error: RPi firmware returned 0x%08x\n", p[1]);
		exit(EXIT_FAILURE);
	}

	memcpy(tag_data, p + i, buf_size);
}

void gpio_set(int mb, int gpio, int state)
{
	uint32_t gpio_set[2];
	gpio_set[0] = gpio;
	gpio_set[1] = state ? 1 : 0;
	rpi_firmware_property(mb, RPI_FIRMWARE_SET_GPIO_STATE, gpio_set, sizeof(gpio_set));
	return;
}

int gpio_get(int mb, int gpio)
{
	uint32_t gpio_set[2];
	gpio_set[0] = gpio;
	rpi_firmware_property(mb, RPI_FIRMWARE_GET_GPIO_STATE, gpio_set, sizeof(gpio_set));
	return gpio_set[1];
}

void gpio_set_config(int mb, int gpio, int dir, int active_low, int term_en, int term_pull_up, int initial_state)
// dir:        0=input, 1=output
// active_low: 0=active high (not inverted), 1=active low (inverted)
// term_en:    0=no termination, 1=pull enabled
// term_pull_up: 0=pull down, 1=pull up (assuming term_en)
// initial_state: State of GPIO if being set to an output
{
	uint32_t gpio_set[6];
	gpio_set[0] = gpio;
	gpio_set[1] = dir;
	gpio_set[2] = active_low;
	gpio_set[3] = term_en;
	gpio_set[4] = term_pull_up;
	gpio_set[5] = initial_state;
	rpi_firmware_property(mb, RPI_FIRMWARE_SET_GPIO_CONFIG, gpio_set, sizeof(gpio_set));
	return;
}

int gpio_get_config(int mb, int gpio, int *output, int *active_low, int *term_en, int *pull_up)
{
	uint32_t gpio_set[5];
	gpio_set[0] = gpio;
	rpi_firmware_property(mb, RPI_FIRMWARE_GET_GPIO_CONFIG, gpio_set, sizeof(gpio_set));
	*output = gpio_set[1];
	*active_low = gpio_set[2];
	*term_en = gpio_set[3];
	*pull_up = gpio_set[4];
	return gpio_set[0];
}

int main(int argc, char *argv[])
{
	int mb = -1;
	int val;

	if (argc < 3 ||
	    ((argv[1][0]=='s' || argv[1][0]=='S') && argc < 4) ||
	    ((argv[1][0]=='c' || argv[1][0]=='C') && argc < 8)
	   ) {
		fprintf(stderr, "error: Invalid the number of the arguments\n");
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	val = atoi(argv[2]);

	mb = rpi_firmware_open();

	if(argc >= 8)
	{
		//The result from atoi should be checked for each argument,
		//but shouldn't go too crazy anyway.
		gpio_set_config(mb, val, atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7]));
		fprintf(stdout, "Set config of %d to %s, active %s, %s, state %s\n", val,
				atoi(argv[3]) ? "output" : "input",
				atoi(argv[4]) ? "low" : "high",
				atoi(argv[5]) ? (atoi(argv[6]) ? "pulled high" : "pulled low") : "no termination",
				atoi(argv[7]) ? "high" : "low"
				);
	}
	else if(argc == 3)
	{
		//Must be get, otherwise we wouldn't have got past the argc check
		int output, active_low, term_en, pull_up;
		int state = gpio_get(mb, val);

		fprintf(stdout, "Get state of %d as %d\n", val, state);

		gpio_get_config(mb, val, &output, &active_low, &term_en, &pull_up);
		fprintf(stdout, "get_config dir %s, active %s, %s\n",
			output ? "output" : "input",
			active_low ? "low (inverted)" : "high",
			term_en ? (pull_up ? "pulled high" : "pulled low") : "not terminated"
			);
	}
	else
	{
		gpio_set(mb, val, atoi(argv[3]));
		fprintf(stdout, "Set state of %d to %d\n", val, atoi(argv[3]));
	}

	rpi_firmware_close(mb);
	mb = -1;

	return 0;
}
