/*
 * Copyright 2026 Various Authors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "lastfm_loved.h"
#include "file.h"
#include "uchar.h"
#include "utils.h"
#include "xmalloc.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>

#define LASTFM_LOVED_BUCKETS 4096

struct lastfm_loved_entry {
	struct lastfm_loved_entry *next;
	char *artist;
	char *title;
};

struct lastfm_loved_set {
	struct lastfm_loved_entry *buckets[LASTFM_LOVED_BUCKETS];
};

static struct lastfm_loved_set *lastfm_loved_set;

static unsigned int lastfm_loved_hash(const char *artist, const char *title)
{
	char *artist_folded = u_casefold(artist);
	char *title_folded = u_casefold(title);
	unsigned int hash = hash_str(artist_folded);

	hash = hash * 33 ^ hash_str(title_folded);
	free(artist_folded);
	free(title_folded);
	return hash;
}

static void lastfm_loved_set_free(struct lastfm_loved_set *set)
{
	int i;

	if (!set)
		return;
	for (i = 0; i < LASTFM_LOVED_BUCKETS; i++) {
		struct lastfm_loved_entry *entry = set->buckets[i];

		while (entry) {
			struct lastfm_loved_entry *next = entry->next;

			free(entry->artist);
			free(entry->title);
			free(entry);
			entry = next;
		}
	}
	free(set);
}

static void lastfm_loved_set_add(struct lastfm_loved_set *set,
		const char *artist, const char *title)
{
	unsigned int bucket = lastfm_loved_hash(artist, title) % LASTFM_LOVED_BUCKETS;
	struct lastfm_loved_entry *entry;

	for (entry = set->buckets[bucket]; entry; entry = entry->next) {
		if (u_strcase_equal(entry->artist, artist) &&
				u_strcase_equal(entry->title, title))
			return;
	}

	entry = xnew(struct lastfm_loved_entry, 1);
	entry->artist = xstrdup(artist);
	entry->title = xstrdup(title);
	entry->next = set->buckets[bucket];
	set->buckets[bucket] = entry;
}

static int lastfm_loved_set_parse(struct lastfm_loved_set *set,
		char *buffer, ssize_t size)
{
	ssize_t pos = 0;

	while (pos < size) {
		char *artist = buffer + pos;
		char *title;
		char *end = memchr(artist, '\0', size - pos);

		if (!end)
			goto invalid;
		pos = end - buffer + 1;
		if (pos == size)
			goto invalid;

		title = buffer + pos;
		end = memchr(title, '\0', size - pos);
		if (!end || artist[0] == '\0' || title[0] == '\0')
			goto invalid;
		pos = end - buffer + 1;

		lastfm_loved_set_add(set, artist, title);
	}
	return 0;
invalid:
	errno = EINVAL;
	return -1;
}

int lastfm_loved_load(const char *filename)
{
	struct lastfm_loved_set *set = xnew0(struct lastfm_loved_set, 1);
	char *buffer = NULL;
	ssize_t size = 0;

	if (filename) {
		buffer = mmap_file(filename, &size);
		if (size < 0)
			goto error;
		if (buffer && lastfm_loved_set_parse(set, buffer, size))
			goto error;
	}

	if (buffer)
		munmap(buffer, size);
	lastfm_loved_set_free(lastfm_loved_set);
	lastfm_loved_set = set;
	return 0;
error:
	{
		int save = errno;

		if (buffer)
			munmap(buffer, size);
		lastfm_loved_set_free(set);
		errno = save;
	}
	return -1;
}

int lastfm_loved_match(const char *artist, const char *title)
{
	unsigned int bucket;
	struct lastfm_loved_entry *entry;

	if (!lastfm_loved_set || !artist || !title)
		return 0;

	bucket = lastfm_loved_hash(artist, title) % LASTFM_LOVED_BUCKETS;
	for (entry = lastfm_loved_set->buckets[bucket]; entry; entry = entry->next) {
		if (u_strcase_equal(entry->artist, artist) &&
				u_strcase_equal(entry->title, title))
			return 1;
	}
	return 0;
}

void lastfm_loved_clear(void)
{
	lastfm_loved_set_free(lastfm_loved_set);
	lastfm_loved_set = NULL;
}
