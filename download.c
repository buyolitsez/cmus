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

#include "download.h"
#include "misc.h"
#include "utils.h"
#include "xmalloc.h"
#include "xstrjoin.h"

#include <string.h>
#include <sys/stat.h>

char *bucket_dir;
char *download_dir;

char *download_dir_option(const char *value)
{
	char *dir;
	size_t len;

	if (!value[0])
		return NULL;
	dir = expand_filename(value);
	len = strlen(dir);
	while (len > 1 && dir[len - 1] == '/')
		dir[--len] = 0;
	return dir;
}

char *download_local_copy(const char *filename)
{
	const char *path = is_cue_url(filename) ? filename + 6 : filename;
	char *local, *slash = NULL;
	struct stat st;
	size_t len;
	int found;

	if (!bucket_dir || !download_dir)
		return NULL;
	len = strlen(bucket_dir);
	if (strncmp(path, bucket_dir, len) || path[len] != '/')
		return NULL;

	local = xstrjoin(download_dir, path + len);
	if (path != filename) {
		/* cue://<file.cue>/<track>: the cue file has to be there */
		slash = strrchr(local, '/');
		*slash = 0;
	}
	found = stat(local, &st) == 0;
	if (slash)
		*slash = '/';
	if (!found) {
		free(local);
		return NULL;
	}
	if (path != filename) {
		char *url = xstrjoin("cue://", local);

		free(local);
		local = url;
	}
	return local;
}

int download_exists(const char *filename)
{
	char *local = download_local_copy(filename);

	free(local);
	return local != NULL;
}
