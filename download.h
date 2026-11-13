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

#ifndef CMUS_DOWNLOAD_H
#define CMUS_DOWNLOAD_H

/*
 * Tracks under bucket_dir (the rclone mount of the bucket) can have a local
 * copy at the same relative path under download_dir. The local copy is
 * played instead of the bucket file.
 */
extern char *bucket_dir;
extern char *download_dir;

/* value of a directory option: ~ expanded, no trailing slash, NULL for "" */
char *download_dir_option(const char *value);
/* the local copy of filename, NULL if there is none */
char *download_local_copy(const char *filename);
int download_exists(const char *filename);

#endif
