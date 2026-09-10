# Video playback through an mpv overlay

Status: implemented.

## Goal

Play video files from kew's library. The video shows in a borderless mpv
window that Hyprland pins over the cover-art square of the kew Ghostty
window, so it looks like part of kew. kew keeps drawing everything else:
metadata, the waveform with its moving playhead, the time line, the queue.
kew's keys stay the only controls.

Decisions already made:

- mpv plays the audio. kew's own sound system is stopped while a video
  plays. The spectrum visualizer goes flat during video.
- Hiding kew with `q` or Esc hides the video too and playback continues.
- Video files live in the normal library tree, playlists and queue.
- The CRT shader is not applied to the mpv window. Porting the shader to
  mpv's GLSL hooks is a later, separate task.

## Out of scope

- Rendering video inside the terminal.
- Any compositor other than Hyprland. The kew side is generic (one shell
  command with a documented environment); only the shipped script is
  Hyprland-specific.
- Video metadata beyond what TagLib already returns. Files without tags
  show the file name as title.
- Following the kew window when the user drags it while a video plays.
  The overlay is re-placed on show, on terminal resize, and on track change.

## Components

### 1. File classification

`src/utils/file.h` gains `VIDEO_EXTENSIONS`, default
`(mp4|mkv|mov|avi|m4v)$`, and `bool is_video_path(const char *path)`.
The library scan admits both regexes. `webm` stays in the audio list, as
today, because kew already decodes its audio and an extension cannot tell
an audio-only webm from a video one. A kewrc key `videoExtensions=` lets
the user override the list, for example to add `webm`.

The video flag is derived from the extension whenever it is needed. Nothing
is stored in `FileSystemEntry`, so `DB_VERSION` does not change.
Nothing is added to `SongData` either; callers ask `is_video_path`.

Library and playlist views keep the extension on video names (audio
names have it stripped), which is enough to tell them apart.

### 2. Video as a codec backend

kew's sound engine selects a decoder through a codec table keyed by
extension (`src/sound/decoders.c`). Each entry is a miniaudio data source
with init, read, seek, cursor and length callbacks. Video is added as one
more entry, decoder type `VIDEO`, implemented in
`src/sound/video_decoder.c`:

- `init` probes the duration with `ffprobe` and records format
  `f32 / 2 channels / 48000 Hz`. It launches nothing, because the engine
  initialises decoders ahead of time to probe files and to preload the
  next track.
- `read` fills the requested frames with silence and advances a cursor.
  The first read starts mpv (or sends `loadfile` to a running mpv when the
  previous track was also a video), so mpv starts exactly when the engine
  switches to this track. `read` returns end-of-stream when mpv reports
  `eof-reached`, when its socket closes, or when the cursor is five seconds
  past the probed length.
- `seek` sends an absolute seek to mpv and moves the cursor.
- `uninit` quits mpv if this decoder is the one mpv is playing.

Because the engine treats it as an ordinary codec, song loading, the
double-decoder preload, end-of-track switching, repeat, shuffle, the
playback clock, the footer and MPRIS all work unchanged. The spectrum
visualizer sees silence and draws flat.

### 3. mpv process and IPC

`src/sound/video_player.c` owns the single mpv instance and its JSON IPC
socket. It is the only place that talks to mpv. `src/sound/video_ipc.c`
holds the socket client and a small line scanner for the few event shapes
mpv sends (`property-change` for `time-pos`, `duration`, `pause`,
`eof-reached`, `idle-active`, and `end-file`). No JSON library is added.

**Launching.** The player runs the kewrc `videoCommand` through a
variant of the existing detached runner that sets these environment
variables:

| Variable | Meaning |
| --- | --- |
| `KEW_VIDEO_ACTION` | `start` or `place` |
| `KEW_VIDEO_FILE` | absolute path of the file (start only) |
| `KEW_VIDEO_SOCKET` | `$XDG_RUNTIME_DIR/kew-mpv.sock` |
| `KEW_VIDEO_ROW`, `KEW_VIDEO_COL` | top-left cell of the cover area, 0-based |
| `KEW_VIDEO_ROWS`, `KEW_VIDEO_COLS` | size of the cover area in cells |
| `KEW_TERM_ROWS`, `KEW_TERM_COLS` | terminal size in cells |
| `KEW_TERM_PX_W`, `KEW_TERM_PX_H` | terminal text area in pixels, or `-1` |

After `start`, kew connects to the socket, retrying for up to three
seconds. Failure is a track error: the read callback returns
end-of-stream at once and the engine moves on, the same path a broken
audio file takes.

**Mirroring controls.** The engine's pause, resume, stop and volume
functions (`src/sound/playback.c`, `src/sound/volume.c`) each call the
player, which does nothing when no video is active. Seeks arrive through
the decoder's seek callback.

### 4. Clock and playhead

The engine's own clock keeps running because the silence stream is paced
by the audio device like any track. Seeks are exact. Drift between the
device clock and mpv over a long file is a few milliseconds and is not
corrected.

### 5. Waveform

`waveform_request` is called with the video path as it is today. The worker
uses the existing codec ops, which cover mp4 and webm audio. If
`find_codec_ops` or decoder init fails and `is_video_path` is true, the
worker extracts the audio with

```
ffmpeg -nostdin -loglevel error -y -i FILE -vn -ac 1 -ar 8000 -f wav TMP
```

into the waveform cache directory, decodes the wav with miniaudio, then
deletes the wav. The envelope is cached under the original path as usual,
so this happens once per file. If ffmpeg is missing or fails, the waveform
is simply absent for that file.

### 6. UI

- When the current song is a video, `component_cover_centered` draws
  nothing and instead hands its rectangle to the player, which re-runs the
  script in `place` mode whenever the rectangle changes. The rectangle
  keeps its size, so the layout does not shift.
- The visualizer needs no change: it reads silence.
- The waveform and time components are unchanged.

### 7. Settings

kewrc gains:

```
# Shell command that starts and positions the video overlay. See
# kew-video.sh for the environment it receives.
videoCommand=$HOME/.config/hypr/hyprland/scripts/kew-video.sh
# Extensions treated as video, regex alternatives.
videoExtensions=mp4|mkv|mov|avi|m4v
```

Both are read, defaulted and written back like `hideCommand`.
`videoCommand` empty means video files are not scanned at all, so users
without a script see no change.

### 8. Hyprland script `kew-video.sh`

Lives next to `music.sh` and `kew-shader.sh`.

`start` mode:

```
mpv --input-ipc-server="$KEW_VIDEO_SOCKET" \
    --class=syny.kewvideo --title=kew-video \
    --no-terminal --really-quiet --no-osc --no-osd-bar \
    --input-default-bindings=no --input-vo-keyboard=no \
    --no-border --force-window=yes --keep-open=no \
    --background-color='#0e1511' --idle=yes \
    "$KEW_VIDEO_FILE" &
```

then waits for the window to appear and falls through to `place`.

`place` mode reads the kew window's position and size from `hyprctl
clients -j`, works out the pixel rectangle of the cover square, and applies
it. Cell size comes from `KEW_TERM_PX_W/H` when they are positive, else
from the window size minus twice the Ghostty padding (10 px, a constant in
the script beside the one in the Ghostty config). Then:

```
float on, pin on, move to (x, y), resize to (w, h),
alterzorder top, no_anim while moving
```

Window rules in `rules.lua` for class `syny.kewvideo`: float, no border,
no rounding, no shadow, no blur, opacity 1.0, `no_focus` so keys keep
going to kew, and no initial focus.

`music.sh` changes: every dispatch that moves, pins, fades or parks the kew
window also applies to the `syny.kewvideo` window when it exists. After
`show`, it runs `kew-video.sh` in `place` mode so the overlay lands back on
the square. The fade animates both windows; the video window fades between
0 and 1.0 rather than 0.9.

## Error handling

| Situation | Behaviour |
| --- | --- |
| `videoCommand` empty | Videos are not scanned; nothing else changes. |
| Script or mpv fails to start | Track marked as error, skip to next, log line. |
| Socket closes mid-play | Treated as end of track. |
| mpv window closed by the user | Same as socket close. |
| ffmpeg missing | No waveform for files the bundled demuxers cannot open. |
| Terminal has no pixel size | Script derives cell size from the window size. |

## Testing

Automated, in a new `tests/` directory built by `make test` (the repo has no tests yet):

- `is_video_path` against the default and an overridden list.
- The IPC scanner against captured mpv event lines: `property-change` for
  each observed property, a success reply, an error reply, a truncated
  line followed by its remainder.
- Environment block formatting for `start` and `place`.
- The silence decoder: reads advance the cursor, a seek moves it, and it
  reports end-of-stream on a simulated `eof-reached`.

Manual, on the Super+F5 window, with a checklist committed alongside the
spec:

- Play mp4, mkv and an audio-only track in sequence: overlay appears,
  disappears, audio resumes through kew.
- Pause, seek both ways, next and previous: mpv follows, playhead follows.
- Waveform present for mp4 and mkv.
- `q` hides both windows, sound continues, Super+F5 brings both back on the
  square.
- Resize the window: overlay tracks the square.
- Kill mpv by hand: kew advances to the next track.
- `Shift+Q`: no mpv process left behind.

## Files touched

- `src/utils/file.h`, `file.c`: extension lists, `is_video_path`.
- `src/data/directorytree.c`: scan admits video extensions when enabled.
- `src/sound/video_decoder.c`, `.h`, `video_player.c`, `.h`,
  `video_ipc.c`, `.h`: new.
- `src/sound/decoders.c`, `sound.c`, `audiotypes.h`: `VIDEO` codec entry.
- `src/sound/playback.c`, `volume.c`: mirror pause, resume, stop, volume.
- `src/sound/waveform.c`: ffmpeg fallback.
- `src/utils/utils.c`: detached runner with environment.
- `src/ui/components.c`, `render_ui.c`: blank cover, video tag, place on
  resize.
- `src/ui/settings.c`, `src/common/model.h`: `videoCommand`,
  `videoExtensions`.
- `Makefile`: new objects, `test` target.
- `tests/`: new.
- Outside the repo: `kew-video.sh`, `music.sh`, `rules.lua`, `kewrc`.
