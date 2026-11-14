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

#include "lyrics.h"
#include "xmalloc.h"
#include "uchar.h"
#include "misc.h"
#include "utils.h"
#include "debug.h"
#include "spawn.h"

#include <stdio.h>
#include <string.h>

struct window *lyrics_win;
struct searchable *lyrics_searchable;
char *lyrics_program;

static LIST_HEAD(lyrics_head);
static char *lyrics_filename;
static int lyrics_synced;

static GENERIC_ITER_PREV(lyrics_get_prev, struct lyrics_line, node)
static GENERIC_ITER_NEXT(lyrics_get_next, struct lyrics_line, node)

static int lyrics_search_get_current(void *data, struct iter *iter, enum search_direction dir)
{
	return window_get_sel(lyrics_win, iter);
}

static int lyrics_search_matches(void *data, struct iter *iter, const char *text)
{
	if (!u_strcasestr(iter_to_lyrics_line(iter)->text, text))
		return 0;
	window_set_sel(lyrics_win, iter);
	return 1;
}

static const struct searchable_ops lyrics_search_ops = {
	.get_prev = lyrics_get_prev,
	.get_next = lyrics_get_next,
	.get_current = lyrics_search_get_current,
	.matches = lyrics_search_matches
};

static void add_line(const char *text, int len, int time_ms)
{
	struct lyrics_line *l = xnew(struct lyrics_line, 1);

	l->text = xstrndup(text, len);
	l->time_ms = time_ms;
	list_add_tail(&l->node, &lyrics_head);
}

static void clear(void)
{
	struct lyrics_line *l, *next;

	list_for_each_entry_safe(l, next, &lyrics_head, node) {
		free(l->text);
		free(l);
	}
	list_init(&lyrics_head);
	lyrics_synced = 0;
}

/* "[mm:ss.xx]" at the start of an LRC line, -1 if there is none */
static int parse_time(const char *s, const char **end)
{
	int min, sec, frac = 0, n = 0;

	if (sscanf(s, "[%d:%d%n", &min, &sec, &n) != 2 || n == 0)
		return -1;
	s += n;
	if (*s == '.' || *s == ':') {
		int digits = 0;

		for (s++; *s >= '0' && *s <= '9'; s++, digits++) {
			if (digits < 3)
				frac = frac * 10 + *s - '0';
		}
		while (digits++ < 3)
			frac *= 10;
	}
	if (*s != ']')
		return -1;
	*end = s + 1;
	return (min * 60 + sec) * 1000 + frac;
}

void lyrics_parse(const char *buf)
{
	clear();
	while (*buf) {
		const char *eol = strchr(buf, '\n');
		const char *text = buf;
		int len, time_ms;

		if (!eol)
			eol = buf + strlen(buf);
		time_ms = parse_time(buf, &text);
		if (time_ms >= 0) {
			const char *t;

			/* several timestamps for one line: keep the first */
			while (parse_time(text, &t) >= 0)
				text = t;
			lyrics_synced = 1;
		} else if (buf[0] == '[' && eol > buf && eol[-1] == ']') {
			/* LRC tag such as [ar:...] */
			buf = *eol ? eol + 1 : eol;
			continue;
		}
		len = eol - text;
		if (len > 0 && text[len - 1] == '\r')
			len--;
		add_line(text, len, time_ms);
		buf = *eol ? eol + 1 : eol;
	}
	window_set_contents(lyrics_win, &lyrics_head);
	lyrics_win->changed = 1;
}

static char *run_program(const char *filename)
{
	char *argv[] = { lyrics_program, (char *)filename, NULL };

	return spawn_output(argv);
}

void lyrics_load(const char *filename)
{
	char *text;

	if (filename && lyrics_filename && !strcmp(filename, lyrics_filename))
		return;
	free(lyrics_filename);
	lyrics_filename = filename ? xstrdup(filename) : NULL;

	text = filename && lyrics_program && *lyrics_program ? run_program(filename) : NULL;
	lyrics_parse(text && *text ? text : "No lyrics");
	free(text);
}

void lyrics_reload(void)
{
	char *filename = lyrics_filename;

	lyrics_filename = NULL;
	lyrics_load(filename);
	free(filename);
}

void lyrics_set_position(int pos_ms)
{
	struct lyrics_line *l, *cur = NULL;
	struct iter iter;

	if (!lyrics_synced)
		return;
	list_for_each_entry(l, &lyrics_head, node) {
		if (l->time_ms < 0 || !l->text[0])
			continue;
		if (l->time_ms > pos_ms)
			break;
		cur = l;
	}
	if (!cur)
		cur = list_entry(lyrics_head.next, struct lyrics_line, node);
	iter.data0 = &lyrics_head;
	iter.data1 = cur;
	iter.data2 = NULL;
	window_set_sel(lyrics_win, &iter);
}

void lyrics_init(void)
{
	struct iter iter;

	lyrics_win = window_new(lyrics_get_prev, lyrics_get_next);
	lyrics_parse("No lyrics");

	iter.data0 = &lyrics_head;
	iter.data1 = NULL;
	iter.data2 = NULL;
	lyrics_searchable = searchable_new(NULL, &iter, &lyrics_search_ops);
}

void lyrics_exit(void)
{
	clear();
	free(lyrics_filename);
	searchable_free(lyrics_searchable);
	window_free(lyrics_win);
}
