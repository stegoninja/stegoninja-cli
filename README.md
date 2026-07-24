# StegoNinja

StegoNinja hides (embeds) and recovers (extracts) a secret file or message inside a cover
**image**, **audio**, or **video** file using steganography. It runs entirely on your
machine with no network access, and ships as **two binaries over one shared engine**:

| Binary | Front-end | Techniques |
|--------|-----------|------------|
| `stegoninja` | scriptable **CLI** (subcommands, `--help`, exit codes) | Image LSB, Image BPCS, Audio LSB, Video LSB |
| `stegoninja-tui` | interactive **ncurses TUI** (menus, file browser, preview + PSNR) | Image LSB, Image BPCS, Audio LSB, Video LSB |

Both front-ends link the same `stegoninja_engine` library, so **every technique and option
is reachable from either binary** — the CLI for automation, the TUI for guided use.

> The HTTP **web API** half of StegoNinja lives in the sibling repo `../stegoninja-api`.
> This repo does not build or run a server.

Techniques:

| Media | Technique |
|-------|-----------|
| Image | LSB — hide a text message **or** an image-in-image |
| Image | BPCS (Bit-Plane Complexity Segmentation) over 24-bit BMP |
| Audio | LSB over WAV PCM |
| Video | LSB over frames (lossless FFV1/AVI) |

Optional **Vigenère** payload obfuscation and password-seeded position **randomization**
are available per technique. Note: the Vigenère scheme is a byte-shift *obfuscation*,
**not strong encryption**.

## Build

Requires a C++17 compiler, CMake, **OpenCV** (image + video), and **ncurses** (the TUI).

```shell
cmake -S . -B build && cmake --build build
```

One build produces exactly two binaries in `build/`: `stegoninja` and `stegoninja-tui`
(plus the `lsb_text_test` engine harness). If OpenCV is missing, CMake warns and builds
nothing runnable; if only ncurses is missing, it builds the CLI alone.

On Debian/Ubuntu:

```shell
sudo apt-get install -y build-essential cmake libopencv-dev libncurses-dev
```

### Building on macOS

```shell
brew install cmake opencv ncurses
cmake -S . -B build && cmake --build build
```

CMake automatically adds the Homebrew prefixes (`/opt/homebrew` on Apple Silicon,
`/usr/local` on Intel), so OpenCV and ncurses are found without extra flags. Homebrew's
OpenCV bundles ffmpeg, so the video technique's lossless FFV1 codec is available. If
ncurses is not found, pass it explicitly:

```shell
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix ncurses);$(brew --prefix opencv)"
```

## CLI usage (`stegoninja`)

```
stegoninja <technique> <embed|extract> [options]
stegoninja --help            # techniques + common options
stegoninja <technique> --help
```

Options share one vocabulary across techniques: `--cover`, `--secret`, `--stego`,
`--output`, `--message` / `--message-file`, `--password` (`--key` alias), `--encrypt`,
`--randomize`, `--seed`, `--frame-mode seq|rand`, `--pixel-mode seq|rand`. Every path is
non-interactive and exits non-zero on error.

```shell
# Image LSB — text, and image-in-image (extract auto-detects which)
stegoninja image-lsb embed   --cover cover.png --message "hi" --output stego.png [--encrypt --password k]
stegoninja image-lsb embed   --cover cover.png --secret secret.png --output stego.png
stegoninja image-lsb extract --stego stego.png [--output out.png] [--encrypt --password k]

# Image BPCS (24-bit BMP cover; --password enables Vigenère + block shuffle)
stegoninja bpcs  embed   --cover cover.bmp --secret file --output stego.bmp [--password k]
stegoninja bpcs  extract --stego stego.bmp --output ./out [--password k]

# Audio LSB (PCM WAV cover)
stegoninja audio embed   --cover cover.wav --secret file --output stego.wav [--encrypt --randomize --password k]
stegoninja audio extract --stego stego.wav [--output ./out] [--encrypt --randomize --password k]

# Video LSB (lossless FFV1/AVI output; --seed is 0..31, stored in-band)
stegoninja video embed   --cover in.avi --message "secret" --output stego.avi [--seed N --frame-mode rand --pixel-mode rand --encrypt --password k]
stegoninja video extract --stego stego.avi [--output out.txt] [--password k]
```

Extraction must use the **same** password, flags, and (video) seed as embedding.

## TUI usage (`stegoninja-tui`)

```shell
./build/stegoninja-tui
```

An arrow-key menu selects the technique, then embed/extract, guiding you through inputs
with a file browser (filtered to each technique's file types). Image embeds show a
side-by-side cover/stego preview and a PSNR score.

Set **`STEGO_NO_PREVIEW=1`** (or run with no `DISPLAY`) to skip the OpenCV preview windows
so it works headless/over SSH; metrics are still reported:

```shell
STEGO_NO_PREVIEW=1 ./build/stegoninja-tui
```

The text and image-in-image formats are tagged with distinct magic markers (`SN1M` /
`SN1I`), so extracting one as the other is rejected rather than silently producing garbage.

## Testing

A scriptable round-trip harness verifies byte-exact embed→extract through the CLI:

```shell
cmake --build build
bash tests/roundtrip.sh
```

It covers audio `{plain, encrypt, randomize, encrypt+randomize}` plus a wrong-password
rejection case, BPCS `{no-password, with-password}`, image LSB text `{plain, encrypted}`,
image-in-image, and video `{plain, encrypted, random frame+pixel}` when ffmpeg is present.
The `lsb_text_test` target additionally exercises the engine's text path directly (round
-trip, encryption, magic-guard rejection, mode detection).

## Packaging & install

```shell
scripts/build.sh                       # configure + build into ./build
scripts/build.sh --install /usr/local  # install binaries to <prefix>/bin
scripts/build.sh --package             # build a dist tarball via CPack (TGZ)
```

`--package` produces `build/stegoninja-<version>-<system>.tar.gz` containing the two
binaries. The project version is set in `CMakeLists.txt` (`project(... VERSION ...)`).

## Relationship to `stegoninja-api`

The steganography engine originated as a shared copy of `../stegoninja-api` (the source of
truth for the hosted service) but has since **intentionally diverged**. Stego files
produced here are no longer guaranteed to interoperate with the API for changed formats,
and there is no ongoing sync requirement. Notable client-line changes:

- Consolidated the four techniques into one shared engine library behind a clean
  embed/extract API, linked by both front-ends.
- Unified everything into two binaries (`stegoninja` CLI + `stegoninja-tui`), each covering
  all four techniques — replacing the previous five separate programs with mixed interfaces.
- Length-prefixed the image text-LSB frame so optional obfuscation can carry any byte.
- Consolidated the Vigenère cipher into a single shared implementation.
- Hardened audio extraction against crashes on wrong password / corrupt input.
- Labeled the Vigenère option as obfuscation, not encryption, throughout.
- Replaced the native `size_t` image length header with a portable little-endian one.
- Tagged the text and image-in-image LSB payloads with distinct format magics.
- Made the video technique fully scriptable and fixed random frame-mode extraction.
- Added a headless (no-preview) mode to the TUI.
