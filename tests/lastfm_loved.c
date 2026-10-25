/*
 * Copyright 2026 Various Authors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "../convert.h"
#include "../file.h"
#include "../lastfm_loved.h"

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char test_charset[] = "UTF-8";
static char test_clipped_text[] = "...";
static char test_program_name[] = "test-lastfm-loved";

char *charset = test_charset;
int using_utf8 = 1;
char *clipped_text_internal = test_clipped_text;
char *program_name = test_program_name;

int utf8_encode(const char *inbuf, const char *encoding, char **outbuf)
{
	return -1;
}

static void write_cache(const char *filename, const char *data, size_t size)
{
	int fd = open(filename, O_WRONLY | O_TRUNC);

	assert(fd != -1);
	assert(write_all(fd, data, size) == (ssize_t)size);
	assert(close(fd) == 0);
}

int main(void)
{
	char filename[] = "/tmp/cmus-lastfm-loved.XXXXXX";
	static const char loved[] = "Björk\0Jóga\0Autechre\0Rae\0";
	int fd = mkstemp(filename);

	assert(fd != -1);
	assert(close(fd) == 0);
	write_cache(filename, loved, sizeof(loved) - 1);
	assert(lastfm_loved_load(filename) == 0);
	assert(lastfm_loved_match("Björk", "Jóga"));
	assert(lastfm_loved_match("AUTECHRE", "rae"));
	assert(!lastfm_loved_match("Autechre", "Bike"));

	write_cache(filename, "broken\0", sizeof("broken\0") - 1);
	assert(lastfm_loved_load(filename) == -1);
	assert(lastfm_loved_match("AUTECHRE", "rae"));

	assert(lastfm_loved_load(NULL) == 0);
	assert(!lastfm_loved_match("Autechre", "Rae"));
	lastfm_loved_clear();
	assert(unlink(filename) == 0);
	return 0;
}
