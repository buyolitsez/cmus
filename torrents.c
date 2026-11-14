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

#include "torrents.h"
#include "xmalloc.h"
#include "uchar.h"
#include "misc.h"
#include "utils.h"
#include "debug.h"
#include "spawn.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

struct window *torrents_win;
struct searchable *torrents_searchable;
char *torrents_program;

static LIST_HEAD(torrents_head);
static time_t loaded;

static GENERIC_ITER_PREV(torrents_get_prev, struct torrent_line, node)
static GENERIC_ITER_NEXT(torrents_get_next, struct torrent_line, node)

static int torrents_search_get_current(void *data, struct iter *iter, enum search_direction dir)
{
	return window_get_sel(torrents_win, iter);
}

static int torrents_search_matches(void *data, struct iter *iter, const char *text)
{
	if (!u_strcasestr(iter_to_torrent_line(iter)->text, text))
		return 0;
	window_set_sel(torrents_win, iter);
	return 1;
}

static const struct searchable_ops torrents_search_ops = {
	.get_prev = torrents_get_prev,
	.get_next = torrents_get_next,
	.get_current = torrents_search_get_current,
	.matches = torrents_search_matches
};

static void clear(void)
{
	struct torrent_line *l, *next;

	list_for_each_entry_safe(l, next, &torrents_head, node) {
		free(l->id);
		free(l->text);
		free(l);
	}
	list_init(&torrents_head);
}

static char *run_program(const char *action, const char *id)
{
	char *argv[] = { torrents_program, (char *)action, (char *)id, NULL };

	return spawn_output(argv);
}

static void parse(const char *buf)
{
	char *sel_id = NULL;
	struct torrent_line *l, *sel = NULL;
	struct iter iter;

	if (window_get_sel(torrents_win, &iter))
		sel_id = xstrdup(iter_to_torrent_line(&iter)->id);
	clear();
	while (*buf) {
		const char *eol = strchr(buf, '\n');
		const char *tab;

		if (!eol)
			eol = buf + strlen(buf);
		tab = memchr(buf, '\t', eol - buf);
		l = xnew(struct torrent_line, 1);
		l->id = tab ? xstrndup(buf, tab - buf) : xstrdup("");
		l->text = tab ? xstrndup(tab + 1, eol - tab - 1) : xstrndup(buf, eol - buf);
		list_add_tail(&l->node, &torrents_head);
		if (sel_id && *sel_id && !strcmp(sel_id, l->id))
			sel = l;
		buf = *eol ? eol + 1 : eol;
	}
	window_set_contents(torrents_win, &torrents_head);
	/* keep the selection on the same torrent */
	if (sel) {
		iter.data0 = &torrents_head;
		iter.data1 = sel;
		iter.data2 = NULL;
		window_set_sel(torrents_win, &iter);
	}
	free(sel_id);
	torrents_win->changed = 1;
}

void torrents_reload(int force)
{
	char *out;

	if (!force && time(NULL) - loaded < 3)
		return;
	loaded = time(NULL);
	out = torrents_program && *torrents_program ? run_program("status", NULL) : NULL;
	parse(out && *out ? out : "\tNo torrents: set torrents_program (contrib/cmus-b2-seed)");
	free(out);
}

void torrents_run(const char *action)
{
	struct iter iter;
	const char *id = "";

	if (!torrents_program || !*torrents_program)
		return;
	if (window_get_sel(torrents_win, &iter))
		id = iter_to_torrent_line(&iter)->id;
	free(run_program(action, id));
	torrents_reload(1);
}

void torrents_init(void)
{
	struct iter iter;

	torrents_win = window_new(torrents_get_prev, torrents_get_next);
	parse("");

	iter.data0 = &torrents_head;
	iter.data1 = NULL;
	iter.data2 = NULL;
	torrents_searchable = searchable_new(NULL, &iter, &torrents_search_ops);
}

void torrents_exit(void)
{
	clear();
	searchable_free(torrents_searchable);
	window_free(torrents_win);
}
