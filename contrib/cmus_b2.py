"""Helpers shared by the cmus-b2-* scripts: cmus-remote, the bucket and the catalog as contrib/cmus-b2 adds it."""
import curses
import gzip
import json
import os
import re
import subprocess
import sys

REMOTE = "b2music:buyolitsez-music"
OUT = os.path.expanduser("~/.cache/cmus-b2")


def cmus(*cmd):
    return subprocess.run(["cmus-remote", *cmd], capture_output=True, text=True).stdout


def query():
    """cmus-remote -Q as a dict: status, file, position, and the options as "set <name>"."""
    q = {}
    for line in cmus("-Q").splitlines():
        if line.startswith("set "):
            k, _, v = line[4:].partition(" ")
            k = "set " + k
        else:
            k, _, v = line.partition(" ")
        q[k] = v
    return q


def option(name):
    """A cmus option, or "" when cmus is not running."""
    return query().get("set " + name, "")


def size(n):
    return f"{n / 2**30:.1f} GB" if n >= 2**30 else f"{n / 2**20:.0f} MB"


def clean(s):
    """As contrib/cmus-b2 writes tags and names."""
    return " ".join(str(s).split())


def catalog_tracks():
    """(id, cmus key, track, album) for every catalog track; the key is the bucket path,
    or (album dir, track number) for a CUE track, as contrib/cmus-b2 adds them."""
    with gzip.open(os.path.join(OUT, "catalog.json.gz"), "rt", encoding="utf-8") as f:
        catalog = json.load(f)
    for al in catalog["albums"]:
        for t in al["tracks"]:
            path = t["path"]
            if "start" in t:
                yield f"{path}#{float(t['start'])}", (os.path.dirname(path), t.get("track")), t, al
            else:
                yield path, path, t, al


def key_of(filename, roots):
    """The catalog key of a cmus file under one of roots (bucket_dir, download_dir), else None."""
    cue = filename.startswith("cue://")
    if cue:
        filename, n = filename[len("cue://"):].rsplit("/", 1)
    for root in roots:
        if root and filename.startswith(root.rstrip("/") + "/"):
            rel = os.path.relpath(filename, root)
            return (os.path.dirname(rel), int(n)) if cue else rel
    return None


def cue_files(cue):
    """The audio files a .cue file refers to and the .cue file itself, last,
    as cmus counts the track downloaded once the .cue file is there."""
    with open(cue, encoding="utf-8", errors="replace") as f:
        names = re.findall(r'^\s*FILE\s+"(.+?)"', f.read(), re.M)
    return [os.path.join(os.path.dirname(cue), n) for n in names] + [cue]


def trash(path):
    subprocess.run(["trash", path] if sys.platform == "darwin" else ["gio", "trash", path], check=True)


def picard(paths):
    """Open paths (files or folders) in a new MusicBrainz Picard window and wait until it is closed."""
    print("Picard is open: save the tags there (Ctrl+S) and close it to go on.")
    subprocess.run(["picard", "--stand-alone-instance", *paths], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def band_drop(path, above):
    """How much quieter, in dB, 30 s in the middle of the track are above `above` Hz than at 11-15 kHz. Encoders
    cut the highs: MP3 at 16-19 kHz, YouTube's streams at 16 kHz, so a file made from a lossy one has next to
    nothing up there (a drop over 45 dB), and a real one has something. None if ffmpeg could not tell."""
    sec = float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path],
                               capture_output=True, text=True).stdout or 0)

    def level(filters):
        out = subprocess.run(["nice", "ffmpeg", "-nostdin", "-hide_banner", "-ss", str(max(sec / 2 - 15, 0)), "-t", "30",
                              "-i", path, "-af", filters + "astats=measure_perchannel=none", "-f", "null", "-"],
                             capture_output=True, text=True).stderr
        m = re.findall(r"RMS level dB: (-?[\d.]+|-inf)", out)
        return float(m[-1]) if m else None

    mid, high = level("highpass=f=11000," * 4 + "lowpass=f=15000," * 4), level(f"highpass=f={above}," * 8)
    return None if mid is None or high is None else mid - high


class Node:
    def __init__(self, name, parent=None, index=None):
        self.name, self.parent, self.index = name, parent, index
        self.children, self.open, self.marked, self.size = {}, False, False, 0
        self.depth = parent.depth + 1 if parent else -1

    def files(self):
        return [self] if self.index is not None else [f for c in self.children.values() for f in c.files()]


def pick(scr, title, files, fmt=size, ordered=False, play=None, at=None, enter="download marked", unit="files", keys=None):
    """Tree of folders and files: arrows move and open folders, Space marks.
    Returns the indices of the marked files, or None. fmt shows the third field of files; ordered keeps
    the order of files instead of sorting by name. play(indices) plays files (a folder, or a file and
    the ones after it in its folder), play([]) stops; it returns what to show in the title. at is a path in the
    tree to open and start on; enter names what Enter does and unit what the leaves are. keys maps more keys
    to (label, run): run(indices) gets the marked files, or the ones under the cursor, outside curses."""
    root = Node("")
    for i, path, n in files:
        node = root
        parts = path.split("/")
        for j, part in enumerate(parts):
            leaf = j == len(parts) - 1
            node = node.children.setdefault(part, Node(part, node, i if leaf else None))
            node.size += n
    root.open = True
    if len(root.children) == 1:  # the torrent's own folder
        next(iter(root.children.values())).open = True
    start = root
    for part in (at or "").split("/"):
        if part not in start.children:
            break
        start = start.children[part]
        start.open = start.index is None

    def visible(node):
        for c in node.children.values() if ordered else sorted(
                node.children.values(), key=lambda c: (c.index is not None, c.name.casefold())):
            yield c
            if c.open:
                yield from visible(c)

    curses.curs_set(0)
    curses.use_default_colors()
    scr.timeout(-1)  # the first screen's one-second refresh stays with the screen
    rows = list(visible(root))
    cur = rows.index(start) if start in rows else 0
    top = max(0, cur - 5)
    playing = ""
    while True:
        rows = list(visible(root))
        cur = max(0, min(cur, len(rows) - 1))
        h, w = scr.getmaxyx()
        page = h - 3
        top = min(max(top, cur - page + 1), cur)
        marked = [f for f in root.files() if f.marked]
        scr.erase()
        scr.addnstr(0, 0, f"{title}   marked: {len(marked)} {unit}, {fmt(sum(f.size for f in marked))}   {playing}", w - 1, curses.A_BOLD)
        for y, node in enumerate(rows[top:top + page], 1):
            fl = node.files()
            box = "[x]" if all(f.marked for f in fl) else "[-]" if any(f.marked for f in fl) else "[ ]"
            name = node.name if node.index is not None else ("▾ " if node.open else "▸ ") + node.name + "/"
            line = f"{box} {'  ' * node.depth}{name}"
            sz = fmt(node.size)
            text = line[:max(0, w - len(sz) - 3)].ljust(w - len(sz) - 3) + " " + sz
            scr.addnstr(y, 0, text, w - 1, curses.A_REVERSE if top + y - 1 == cur else 0)
        more = ("  p play  s stop" if play else "") + "".join(f"  {k} {label}" for k, (label, _) in (keys or {}).items())
        scr.addnstr(h - 1, 0, f"↑↓ move  → open  ← close  Space mark  a all{more}  Enter {enter}  q quit", w - 1, curses.A_DIM)
        k = scr.get_wch()
        node = rows[cur] if rows else None
        if k in (curses.KEY_UP, "k"):
            cur -= 1
        elif k in (curses.KEY_DOWN, "j"):
            cur += 1
        elif k == curses.KEY_PPAGE:
            cur -= page
        elif k == curses.KEY_NPAGE:
            cur += page
        elif k in (curses.KEY_RIGHT, "l") and node.index is None:
            node.open = True
        elif k in (curses.KEY_LEFT, "h"):
            if node.index is None and node.open:
                node.open = False
            elif node.parent is not root:
                node.parent.open = False
                cur = rows.index(node.parent)
        elif k == " ":
            fl = node.files()
            on = not all(f.marked for f in fl)
            for f in fl:
                f.marked = on
            cur += 1
        elif k == "a":
            fl = root.files()
            on = not all(f.marked for f in fl)
            for f in fl:
                f.marked = on
        elif k == "p" and play and node:
            fl = node.files()
            if node.index is not None:  # this file and the ones after it
                fl = node.parent.files()
                fl = fl[fl.index(node):]
            playing = play([f.index for f in fl])
        elif k == "s" and play:
            playing = play([])
        elif keys and k in keys and node:
            curses.def_prog_mode()
            curses.endwin()
            keys[k][1]([f.index for f in marked] or [f.index for f in node.files()])
            curses.reset_prog_mode()
            scr.clear()
        elif k in ("\n", "\r", curses.KEY_ENTER):
            return [f.index for f in marked]
        elif k == "q":
            return None
