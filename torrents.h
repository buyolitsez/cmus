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

#ifndef CMUS_TORRENTS_H
#define CMUS_TORRENTS_H

#include "list.h"
#include "window.h"
#include "search.h"

/* the Torrents view: lines of "torrents_program status", "<id>\t<text>" */
struct torrent_line {
	struct list_head node;
	char *id;
	char *text;
};

static inline struct torrent_line *iter_to_torrent_line(struct iter *iter)
{
	return iter->data1;
}

extern struct window *torrents_win;
extern struct searchable *torrents_searchable;
extern char *torrents_program;

void torrents_init(void);
void torrents_exit(void);
/* reruns "torrents_program status", at most every few seconds unless forced */
void torrents_reload(int force);
/* runs "torrents_program <action> <id of the selected line>", the id is empty on lines without one */
void torrents_run(const char *action);

#endif
