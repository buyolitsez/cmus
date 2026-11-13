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

#ifndef CMUS_LYRICS_H
#define CMUS_LYRICS_H

#include "list.h"
#include "window.h"
#include "search.h"

/* lyrics of the current track, from lyrics_program (plain text or LRC) */
struct lyrics_line {
	struct list_head node;
	char *text;
	/* -1 if the line has no timestamp */
	int time_ms;
};

static inline struct lyrics_line *iter_to_lyrics_line(struct iter *iter)
{
	return iter->data1;
}

extern struct window *lyrics_win;
extern struct searchable *lyrics_searchable;
extern char *lyrics_program;

void lyrics_init(void);
void lyrics_exit(void);
void lyrics_parse(const char *buf);
/* runs lyrics_program unless the lyrics of this file are already loaded */
void lyrics_load(const char *filename);
void lyrics_reload(void);
/* selects the line being sung for synced lyrics */
void lyrics_set_position(int pos_ms);

#endif
