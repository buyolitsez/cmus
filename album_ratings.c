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

#include "album_ratings.h"
#include "file.h"
#include "uchar.h"
#include "utils.h"
#include "xmalloc.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define ALBUM_RATINGS_BUCKETS 1024

struct album_rating {
	struct album_rating *next;
	char *artist;
	char *album;
	int rating;
};

static struct album_rating **album_ratings;

static unsigned int album_ratings_hash(const char *artist, const char *album)
{
	char *artist_folded = u_casefold(artist);
	char *album_folded = u_casefold(album);
	unsigned int hash = hash_str(artist_folded) * 33 ^ hash_str(album_folded);

	free(artist_folded);
	free(album_folded);
	return hash % ALBUM_RATINGS_BUCKETS;
}

static void album_ratings_free(struct album_rating **set)
{
	int i;

	if (!set)
		return;
	for (i = 0; i < ALBUM_RATINGS_BUCKETS; i++) {
		struct album_rating *e = set[i];

		while (e) {
			struct album_rating *next = e->next;

			free(e->artist);
			free(e->album);
			free(e);
			e = next;
		}
	}
	free(set);
}

/* artist \0 album \0 rating \0, repeated */
static int album_ratings_parse(struct album_rating **set, char *buffer, ssize_t size)
{
	ssize_t pos = 0;

	while (pos < size) {
		char *field[3];
		struct album_rating *e;
		unsigned int bucket;
		int i;

		for (i = 0; i < 3; i++) {
			char *end = pos < size ? memchr(buffer + pos, '\0', size - pos) : NULL;

			if (!end) {
				errno = EINVAL;
				return -1;
			}
			field[i] = buffer + pos;
			pos = end - buffer + 1;
		}
		bucket = album_ratings_hash(field[0], field[1]);
		e = xnew(struct album_rating, 1);
		e->artist = xstrdup(field[0]);
		e->album = xstrdup(field[1]);
		e->rating = atoi(field[2]);
		e->next = set[bucket];
		set[bucket] = e;
	}
	return 0;
}

int album_ratings_load(const char *filename)
{
	struct album_rating **set = xnew0(struct album_rating *, ALBUM_RATINGS_BUCKETS);
	char *buffer = NULL;
	ssize_t size = 0;

	if (filename) {
		buffer = mmap_file(filename, &size);
		if (size < 0 || (buffer && album_ratings_parse(set, buffer, size))) {
			int save = errno;

			if (buffer)
				munmap(buffer, size);
			album_ratings_free(set);
			errno = save;
			return -1;
		}
	}
	if (buffer)
		munmap(buffer, size);
	album_ratings_free(album_ratings);
	album_ratings = set;
	return 0;
}

int album_rating(const char *artist, const char *album)
{
	struct album_rating *e;

	if (!album_ratings || !artist || !album)
		return -1;
	for (e = album_ratings[album_ratings_hash(artist, album)]; e; e = e->next) {
		if (u_strcase_equal(e->artist, artist) && u_strcase_equal(e->album, album))
			return e->rating;
	}
	return -1;
}

void album_ratings_clear(void)
{
	album_ratings_free(album_ratings);
	album_ratings = NULL;
}
