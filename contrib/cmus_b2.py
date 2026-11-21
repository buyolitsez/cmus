"""Helpers shared by the cmus-b2-* scripts: cmus-remote, the bucket and the catalog as contrib/cmus-b2 adds it."""
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
