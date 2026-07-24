# StegoNinja CLI/TUI

StegoNinja hides (embeds) and recovers (extracts) a secret file or message inside a cover
**image**, **audio**, or **video** file using steganography. This repository is the
**offline CLI / TUI client** — C++17 programs that run entirely on your machine with no
network access.

> The HTTP **web API** half of StegoNinja lives in the sibling repo `../stegoninja-api`.
> This repo does not build or run a server.

Supported techniques:

| Media | Technique | Tool |
|-------|-----------|------|
| Image | LSB (text & image-in-image) | `SteganoImgLsb` (ncurses TUI) |
| Image | BPCS (Bit-Plane Complexity Segmentation) | `imgBPCSEmbed` / `imgBPCSExtract` |
| Audio | LSB over WAV PCM | `audio` |
| Video | LSB over frames (lossless FFV1/AVI) | `SteganoVid` |

Optional **Vigenère** payload obfuscation and password-seeded position **randomization**
are available on several tools. Note: the Vigenère scheme is a byte-shift *obfuscation*,
**not strong encryption**.

## Build

Requires a C++17 compiler and CMake. **OpenCV** and **ncurses** are optional — they are
only needed for the image LSB TUI and the video tool.

```shell
cmake -S . -B build && cmake --build build
```

- Always built (no OpenCV needed): `audio`, `imgBPCSEmbed`, `imgBPCSExtract`.
- Built only when OpenCV **and** ncurses are found: `SteganoImgLsb`, `SteganoVid`.
  If they are missing, CMake prints a warning and skips those two targets.

To get the full toolset on Debian/Ubuntu:

```shell
sudo apt-get install -y build-essential cmake libopencv-dev libncurses-dev
```

Binaries are written to `build/`. `cmake --install build --prefix <dir>` installs them.

### Building on macOS

Install the dependencies with Homebrew, then build as above:

```shell
brew install cmake opencv ncurses
cmake -S . -B build && cmake --build build
```

CMake automatically adds the Homebrew prefixes (`/opt/homebrew` on Apple Silicon,
`/usr/local` on Intel) to its search path, so OpenCV and ncurses are found without extra
flags. Homebrew's OpenCV bundles ffmpeg, so the video tool's lossless FFV1 codec is
available. If ncurses is not found, pass it explicitly:

```shell
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix ncurses);$(brew --prefix opencv)"
```

The standalone `audio`, `imgBPCSEmbed`, and `imgBPCSExtract` tools need only a C++17
compiler and build on macOS with no extra dependencies.

## Usage

### Audio LSB (`audio`)

```shell
audio embed  <cover.wav> <secret> <output.wav> [password] [-e] [-r]
audio extract <stego.wav>              [password] [-e] [-r]
```

`-e` Vigenère-encrypts the payload with `password`; `-r` randomizes sample positions using
`password` as the seed. Extraction must use the **same** password and `-e`/`-r` flags.

### Image BPCS (`imgBPCSEmbed` / `imgBPCSExtract`)

```shell
imgBPCSEmbed  <cover.bmp> <secret_file> <output.bmp>   # prompts for a password (blank = none)
imgBPCSExtract <stego.bmp> <output_directory>          # prompts for the same password
```

Covers must be 24-bit BMP. A non-empty password enables encryption + randomization.

### Image LSB TUI (`SteganoImgLsb`)

```shell
./build/SteganoImgLsb
```

An interactive ncurses menu: embed/extract a **text message** or an **image-in-image**,
with a side-by-side cover/stego preview and a PSNR score. Requires OpenCV, ncurses, and a
display (it opens OpenCV preview windows).

### Video LSB (`SteganoVid`)

```shell
./build/SteganoVid
```

Interactive prompts for cover/output paths and a seed; output is a lossless FFV1/AVI.
Requires OpenCV.

## Testing

A scriptable round-trip harness verifies byte-exact embed→extract:

```shell
cmake --build build          # build the tools first
bash tests/roundtrip.sh      # audio + BPCS always; video when available
```

It covers audio `{plain, -e, -r, -e -r}` (plus a wrong-password rejection case) and BPCS
`{no-password, with-password}`. Video is exercised when a non-interactive `SteganoVid` is
present; the interactive image TUI is out of scope for the harness.

## Packaging & install

```shell
scripts/build.sh                       # configure + build into ./build
scripts/build.sh --install /usr/local  # install binaries to <prefix>/bin
scripts/build.sh --package             # build a dist tarball via CPack (TGZ)
```

`--package` produces `build/stegoninja-cli-<version>-<system>.tar.gz` containing the
built binaries. The project version is set in `CMakeLists.txt` (`project(... VERSION ...)`).

## Relationship to `stegoninja-api`

The steganography engine originated as a shared copy of `../stegoninja-api` (the source of
truth for the hosted service). This client has since **intentionally diverged** from that
upstream — stego files produced here are no longer guaranteed to interoperate with the API
for changed formats. Notable client-only changes:

- Consolidated the Vigenère cipher into a single shared implementation.
- Hardened audio extraction against crashes on wrong password / corrupt input.
- Made the CMake build tolerate a missing OpenCV and build the standalone tools.
- Labeled the Vigenère option as obfuscation, not encryption, throughout.
