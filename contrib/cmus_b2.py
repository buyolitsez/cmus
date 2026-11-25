"""Helpers shared by the cmus-b2-* scripts: cmus-remote, the bucket and the catalog as contrib/cmus-b2 adds it."""
import curses
import gzip
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

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


def load_env():
    env = os.path.expanduser("~/.config/cmus-b2/env")
    if os.path.exists(env):
        for line in open(env, encoding="utf-8"):
            k, _, v = line.strip().partition("=")
            if k and not k.startswith("#"):
                os.environ.setdefault(k, v.strip().strip("'\""))


BANDS = 500  # Hz: the spectrum of a track is kept in bands this wide


def analyze(path, rate, channels):
    """One full decode of a track: what ffmpeg reports wrong, the dynamic range as foobar's DR Meter measures it
    (per channel, the RMS of the loudest 20% of 3 s blocks against the second highest block peak), the peak and
    RMS in dBFS, and the average spectrum in BANDS-wide bands (dB), which shows a cut made by a lossy encoder."""
    import numpy as np  # here, as the other scripts do without it
    block = int(rate * 3)
    with tempfile.TemporaryFile() as err:
        p = subprocess.Popen(["ffmpeg", "-nostdin", "-v", "error", "-i", path, "-map", "0:a:0", "-f", "f32le",
                              "-acodec", "pcm_f32le", "-"], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=err)
        rms, peaks, square, frames = [], [], np.zeros(channels), 0
        window, power = np.hanning(8192), np.zeros(4097)
        while buf := p.stdout.read(block * channels * 4):
            x = np.frombuffer(buf[:len(buf) // (4 * channels) * 4 * channels], np.float32).reshape(-1, channels)
            sq = (x.astype(np.float64) ** 2).sum(axis=0)
            square, frames = square + sq, frames + len(x)
            peaks.append(np.abs(x).max(axis=0))
            if len(x) == block:  # the last, shorter block counts for the peaks only, as in DR Meter
                rms.append(np.sqrt(2 * sq / len(x)))
            if len(x) >= 8192:
                power += np.abs(np.fft.rfft(x[:8192].mean(axis=1) * window)) ** 2
        p.wait()
        err.seek(0)
        errors = err.read().decode("utf-8", "replace").strip()
    if not frames:
        return errors or "nothing decoded", None
    peaks = np.sort(np.array(peaks), axis=0)
    second = peaks[-2] if len(peaks) > 1 else peaks[-1]
    dr = []
    for ch in range(channels):
        loud = np.sort([b[ch] for b in rms])[-max(1, int(len(rms) * 0.2)):] if rms else np.array([0.0])
        top = np.sqrt(np.mean(loud ** 2))
        dr.append(20 * np.log10(second[ch] / top) if top > 0 and second[ch] > 0 else 0.0)
    freqs = np.fft.rfftfreq(8192, 1 / rate)
    db = 10 * np.log10(power + 1e-20)
    bands = [round(float(db[(freqs >= f) & (freqs < f + BANDS)].mean()), 1) for f in range(0, rate // 2 - BANDS + 1, BANDS)]
    level = lambda v: round(float(20 * np.log10(max(v, 1e-10))), 2)
    return errors, {"dr": round(float(np.mean(dr)), 2), "peak": level(peaks[-1].max()),
                    "rms": level(float(np.sqrt(2 * square.mean() / frames))), "bands": bands}


def lossy_cut(bands):
    """The steepest fall of the spectrum between 14 and 21 kHz, in dB over 1.5 kHz, and where it is (Hz).
    A lossy encoder cuts sharply (at 15-17 kHz for MP3), a real recording rolls off gently: a fall of 9 dB
    or more is a cut (measured on this collection: 10-18 dB on the tracks cut at 16.5 kHz, at most 7 elsewhere)."""
    falls = [(bands[i - 2] - bands[i + 1], i * BANDS) for i in range(14000 // BANDS, min(21000 // BANDS, len(bands) - 1))]
    return max(falls, default=(0, None))


AUCDTECT = "~/.local/share/aucdtect/auCDtect.exe"  # or AUCDTECT=... in ~/.config/cmus-b2/env


def aucdtect_exe():
    """auCDtect.exe when it and wine are there, else None."""
    load_env()
    exe = os.path.expanduser(os.environ.get("AUCDTECT", AUCDTECT))
    return exe if shutil.which("wine") and os.path.isfile(exe) else None


def aucdtect(path, exe):
    """auCDtect's report on a track (the check of CD audio rutracker asks for: CDDA, or MPEG for a file made
    from a lossy one), run in wine on a 16 bit 44.1 kHz WAV of it."""
    with tempfile.TemporaryDirectory(prefix="cmus-b2-aucdtect-") as tmp:
        wav = os.path.join(tmp, os.path.splitext(os.path.basename(path))[0] + ".wav")
        subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-map", "0:a:0", "-c:a", "pcm_s16le", "-ar", "44100", wav],
                       capture_output=True, stdin=subprocess.DEVNULL)
        out = subprocess.run(["nice", "wine", exe, "-m0", os.path.basename(wav)], cwd=tmp, stdin=subprocess.DEVNULL,
                             capture_output=True, text=True, errors="replace",
                             env={**os.environ, "WINEDEBUG": "-all"}).stdout
    lines = [l.strip() for l in out.replace("\r", "\n").splitlines()]
    return "\n".join(l for l in lines if l and not re.fullmatch(r"[\d.%\s|=#*-]*", l))  # not the progress


def aucdtect_verdict(report):
    """'CDDA 100%' or 'MPEG 98%' from auCDtect's report, or ''."""
    m = re.search(r"looks like (\w+)(?:\s+with probability\s+(\d+)%)?", report or "")
    return f"{m[1]} {m[2]}%" if m and m[2] else m[1] if m else ""


class Node:
    def __init__(self, name, parent=None, index=None):
        self.name, self.parent, self.index = name, parent, index
        self.children, self.open, self.marked, self.size = {}, False, False, 0
        self.depth = parent.depth + 1 if parent else -1

    def files(self):
        return [self] if self.index is not None else [f for c in self.children.values() for f in c.files()]


def pick(scr, title, files, fmt=size, ordered=False, play=None, at=None, enter="download marked", unit="files", keys=None,
         kind=None):
    """Tree of folders and files: arrows move and open folders, Space marks.
    Returns the indices of the marked files, or None. fmt shows the third field of files; ordered keeps
    the order of files instead of sorting by name. play(indices) plays files (a folder, or a file and
    the ones after it in its folder), play([]) stops; it returns what to show in the title. at is a path in the
    tree to open and start on; enter names what Enter does and unit what the leaves are. keys maps more keys
    to (label, run): run(indices) gets the marked files, or the ones under the cursor, outside curses. kind(name) gives
    a top folder's kind, such as its format, or None: f then cycles through showing only the folders of one kind."""
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

    kinds = [None]  # None shows all
    if kind:
        counts = {}
        for name in root.children:
            if kind(name):
                counts[kind(name)] = counts.get(kind(name), 0) + 1
        kinds += sorted(counts, key=counts.get, reverse=True)
    shown = None

    def visible(node):
        for c in node.children.values() if ordered else sorted(
                node.children.values(), key=lambda c: (c.index is not None, c.name.casefold())):
            if shown and node is root and kind(c.name) != shown:
                continue
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
        only = f"   only {shown} ({sum(c.parent is root for c in rows)})" if shown else ""
        scr.addnstr(0, 0, f"{title}{only}   marked: {len(marked)} {unit}, {fmt(sum(f.size for f in marked))}   {playing}",
                    w - 1, curses.A_BOLD)
        for y, node in enumerate(rows[top:top + page], 1):
            fl = node.files()
            box = "[x]" if all(f.marked for f in fl) else "[-]" if any(f.marked for f in fl) else "[ ]"
            name = node.name if node.index is not None else ("▾ " if node.open else "▸ ") + node.name + "/"
            line = f"{box} {'  ' * node.depth}{name}"
            sz = fmt(node.size)
            text = line[:max(0, w - len(sz) - 3)].ljust(w - len(sz) - 3) + " " + sz
            scr.addnstr(y, 0, text, w - 1, curses.A_REVERSE if top + y - 1 == cur else 0)
        more = ("  p play  s stop" if play else "") + "".join(f"  {k} {label}" for k, (label, _) in (keys or {}).items())
        if len(kinds) > 2:
            more += f"  f show {kinds[(kinds.index(shown) + 1) % len(kinds)] or 'all'}"
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
        elif k == "f" and len(kinds) > 2:
            shown = kinds[(kinds.index(shown) + 1) % len(kinds)]
            cur = top = 0
        elif k == "a":
            fl = [f for c in rows if c.parent is root for f in c.files()]
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
