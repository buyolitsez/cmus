# cmus — C\* Music Player

https://cmus.github.io/

[![Build Status](https://github.com/cmus/cmus/actions/workflows/build.yml/badge.svg)](https://github.com/cmus/cmus/actions/workflows/build.yml)

Copyright © 2004-2008 Timo Hirvonen <tihirvon@gmail.com>

Copyright © 2008-2017 Various Authors


## Configuration

    $ ./configure

By default, features are auto-detected. To list all configuration options, run
`./configure --help`. Some common autoconf-style options like `--prefix` are
also available.

After running configure you can see from the generated `config.mk` file
what features have been configured in (see the `CONFIG_*` options).

The packages containing dependencies on common distributions are listed below. All dependencies other than pkg-config and ncurses, iconv, and elogind/systemd are for optional input/output plugins. It is assumed that libc headers, a C compiler, git, and GNU Make are available.

| Distro            | Dependencies |
| :--               | :--          |
| **Debian/Ubuntu** | apt install pkg-config libncursesw5-dev libfaad-dev libao-dev libasound2-dev libavformat-dev libavcodec-dev libswresample-dev libflac-dev libjack-dev libmad0-dev libmodplug-dev libmpcdec-dev libsystemd-dev libopusfile-dev libpulse-dev libsamplerate0-dev libsndio-dev libvorbis-dev libwavpack-dev |
| **Fedora/RHEL**   | dnf install 'pkgconfig(ncursesw)' 'pkgconfig(alsa)' 'pkgconfig(ao)' 'pkgconfig(libavformat)' 'pkgconfig(libavcodec)' 'pkgconfig(libswresample)' 'pkgconfig(flac)' 'pkgconfig(jack)' 'pkgconfig(mad)' 'pkgconfig(libmodplug)' libmpcdec-devel 'pkgconfig(libsystemd)' 'pkgconfig(opusfile)' 'pkgconfig(libpulse)' 'pkgconfig(samplerate)' 'pkgconfig(vorbisfile)' 'pkgconfig(wavpack)' |
| **+ RPMFusion**   | dnf install faad2-devel libmp4v2-devel |
| **Arch Linux**    | pacman -S pkg-config ncurses libiconv faad2 alsa-lib libao ffmpeg flac jack libmad libmodplug libmp4v2 libmpcdec systemd opusfile libpulse libsamplerate libvorbis wavpack |
| **Alpine**        | apk add pkgconf ncurses-dev gnu-libiconv-dev alsa-lib-dev libao-dev ffmpeg-dev flac-dev jack-dev libmad-dev libmodplug-dev elogind-dev opus-dev opusfile-dev pulseaudio-dev libsamplerate-dev libvorbis-dev wavpack-dev |
| **Termux**        | apt install libandroid-support ncurses libiconv ffmpeg libmad libmodplug opusfile pulseaudio libflac libvorbis libwavpack |
| **Homebrew**      | brew install pkg-config ncurses faad2 libao ffmpeg flac jack mad libmodplug mp4v2 musepack opusfile libsamplerate libvorbis wavpack |


## Building

    $ make

Or on some BSD systems you need to explicitly use GNU make:

    $ gmake


## Installation

    $ make install

Or to install to a temporary directory:

    $ make install DESTDIR=~/tmp/cmus

This is useful when creating binary packages.

Remember to replace `make` with `gmake` if needed.


## Manuals

    $ man cmus-tutorial

And

    $ man cmus


## IRC Channel

Feel free to join IRC channel #cmus on Libera.chat and share you experience,
problems and issues. Note: This is an unofficial channel and all people hanging
around there are for the love of cmus.


## Reporting Bugs

Bugs should be reported using the GitHub [issue
tracker](https://github.com/cmus/cmus/issues). When creating a new issue, a
template will be shown containing instructions on how to collect the necessary
information.

Additional debug information can be found in `~/cmus-debug.txt` if you
configured cmus with maximum debug level (`./configure DEBUG=2`). In case of a
crash the last lines may be helpful.


## Git Repository

https://github.com/cmus/cmus

    $ git clone https://github.com/cmus/cmus.git


## Hacking

cmus uses the [Linux kernel coding
style](https://www.kernel.org/doc/html/latest/process/coding-style.html). Use
hard tabs. Tabs are _always_ 8 characters wide. Keep the style consistent with
rest of the code.

Bug fixes and implementations of new features should be suggested as a
[pull request](https://github.com/cmus/cmus/pulls) directly on GitHub.



## Music from the B2 bucket

Tracks from the bucket of [b2-player](https://github.com/buyolitsez/b2-player) are played from an rclone mount
of `music/`, with its disk cache; the app's catalog gives the tags, so nothing is read over the network to fill the library.

```bash
cp contrib/rclone-b2music.service ~/.config/systemd/user/
systemctl --user enable --now rclone-b2music   # mounts b2music:buyolitsez-music/music at ~/b2music
contrib/cmus-b2                                # catalog and likes -> ~/.cache/cmus-b2, adds them to a running cmus
```

Lyrics (view 8, synced LRC lines are highlighted): `:set lyrics_program=<repo>/contrib/cmus-b2-lyrics` and, with an
existing autosave, `:bind common 8 view lyrics`. Bucket tracks use the lyrics `cmus-b2` saved from `lyrics/`, local
files a `.lrc` next to them or the lyrics tag (needs `python-mutagen`).

Light theme like the app: `:colorscheme b2` (colors and row formats with ♥ for likes, ↓ for downloaded tracks and ★ for
album ratings).

Downloading: with `set bucket_dir=~/b2music` and `set download_dir=~/Music`, a bucket track that has a copy at the same
path under `download_dir` plays from the copy. `:bind common d run <repo>/contrib/cmus-b2-download` copies the selected
tracks or albums there (in the background, keeping mtimes, so `sync-music.sh` has nothing to upload).

Continuing what was played on the phone: add `cmus-b2-now "$@" &` to the `status_display_program` script, so cmus
writes the track and position to `state/now-playing.json` on play, pause and track change, as the app does, and the app
offers to continue it. When cmus starts or stops after the phone played, the command line says what it was;
`:bind common R shell <repo>/contrib/cmus-b2-now --resume` plays it from where the phone stopped.

Assistant: `:bind common O shell footclient -N <repo>/contrib/cmus-b2-ask` opens a chat with Claude, the same assistant
as in the app (b2-player's `core/`): it finds and plays music in this cmus, answers about listening history and changes
likes and playlists after `y`. Install it from b2-player as its README says (JDK 21); it uses `ANTHROPIC_API_KEY=...` in
`~/.config/cmus-b2/env`.

Filters: `H` shows only liked tracks, `A` all tracks again (`:filter lastfm_loved`, `:filter downloaded`, `:filter`).

Adding albums from a torrent: `:bind common T shell footclient -N <repo>/contrib/cmus-b2-add` (or `a` in the torrents
view) opens a window with the files of the newest `.torrent` with music in `~/Downloads` as a tree (Space marks folders
or files). Claude suggests where each album goes in `~/Music` and changes the plan by what you write ("this is art pop", a path) until you accept it, then the marked files
are downloaded, uploaded with `sync-music.sh`, the catalog is republished and the cmus library refreshed.
`set added_local_copy=ask` (default), `delete` or `keep` decides what happens to the local copy. Needs
`python-libtorrent`, `python-anthropic` and `ANTHROPIC_API_KEY=...` in `~/.config/cmus-b2/env`.

Seeding: `contrib/cmus-b2-seed.service` (edit the path, then `systemctl --user enable --now cmus-b2-seed`) seeds the
added torrents from the bucket mount, with the files at their library paths and nothing downloaded; files whose tags
were changed later are just not seeded. View 9 lists the torrents (`:set torrents_program=<repo>/contrib/cmus-b2-seed`
and, with an existing autosave, `:bind common 9 view torrents`, `:bind torrents a torrent new`,
`:bind torrents space torrent toggle`, `:bind torrents D torrent remove`): seeding on or off, speed, peers, what was sent
this month and in total. What is sent is read from the bucket, so a month is capped by `SEED_LIMIT_GB` in
`~/.config/cmus-b2/env` (default 100): a warning in cmus at 80%, seeding pauses at 100% until the next month.

In `~/.config/cmus/rc`: `set lastfm_loved_file=~/.cache/cmus-b2/likes` (likes from the catalog and `state/likes.json`)
and `set album_ratings_file=~/.cache/cmus-b2/ratings` (album ratings from `state/ratings.json`).
Run `contrib/cmus-b2` again after the catalog changes; `:update-cache` drops tracks deleted from the bucket.

Likes sync both ways with the phone through `state/likes.json`. `:bind common K run cmus-b2-like` likes the selected
tracks (or unlikes them if all are liked; `cmus-b2-like --like` only likes them); the change goes on top of the bucket
copy, so likes from the phone are kept.
`contrib/cmus-b2-likes.timer` brings likes from the phone within a minute (`cmus-b2 --likes`, install as written in it).

Album ratings 1-10 come from the listening diary through `state/ratings.json` (see b2-player's `tools/sync-ratings.py`)
and show as ★ in the `b2` theme. `:bind common * push run cmus-b2-rate ` lets `*`, a number and Enter rate the albums of the selected tracks
(the same as `:run cmus-b2-rate 8`) (it goes to the diary
through the same file); `:filter rating>=8` shows albums rated 8 and up, `:filter rating=-1` the unrated ones.
