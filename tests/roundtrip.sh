#!/usr/bin/env bash
# Round-trip test matrix for the unified StegoNinja CLI (`stegoninja`).
#
# Every technique x option combination is driven non-interactively through the
# single `stegoninja` binary, verifying embed -> extract recovers the secret.
# The interactive `stegoninja-tui` shares the same engine, so these cases also
# exercise the logic it relies on (an engine-level test covers TUI-only paths).
#
# Usage:  tests/roundtrip.sh [build_dir]
# Exit:   0 = all executed cases passed, 1 = a failure, 2 = setup error.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
TMP="$ROOT/tests/tmp"
PASS=0; FAIL=0; SKIP=0

log()  { printf '%s\n' "$*"; }
ok()   { PASS=$((PASS+1)); printf '  PASS  %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); printf '  FAIL  %s\n' "$1"; }
skip() { SKIP=$((SKIP+1)); printf '  SKIP  %s\n' "$1"; }

command -v python3 >/dev/null || { echo "python3 required for fixtures"; exit 2; }
rm -rf "$TMP"; mkdir -p "$TMP"
python3 "$ROOT/tests/gen_fixtures.py" "$TMP" >/dev/null || { echo "fixture generation failed"; exit 2; }

CLI="$BUILD/stegoninja"
if [ ! -x "$CLI" ]; then
  echo "stegoninja CLI not built at $CLI (need OpenCV); nothing to test."; exit 2
fi

# ---------------------------------------------------------------- audio LSB ---
log "audio LSB (WAV):"
audio_case() { # label, flags...
  local label="$1"; shift
  local d="$TMP/a_$RANDOM"; mkdir -p "$d/out"
  "$CLI" audio embed --cover "$TMP/cover.wav" --secret "$TMP/secret.bin" \
         --output "$d/stego.wav" --password pw "$@" >/dev/null 2>&1
  "$CLI" audio extract --stego "$d/stego.wav" --output "$d/out" \
         --password pw "$@" >/dev/null 2>&1
  if cmp -s "$d/out/secret.bin" "$TMP/secret.bin"; then ok "$label"; else bad "$label"; fi
}
audio_case "plain"
audio_case "encrypt (--encrypt)" --encrypt
audio_case "randomize (--randomize)" --randomize
audio_case "encrypt+randomize" --encrypt --randomize
# negative: wrong password must fail cleanly (non-zero), not crash, not recover
d="$TMP/a_neg"; mkdir -p "$d/out"
"$CLI" audio embed --cover "$TMP/cover.wav" --secret "$TMP/secret.bin" \
       --output "$d/stego.wav" --password rightpw --encrypt >/dev/null 2>&1
"$CLI" audio extract --stego "$d/stego.wav" --output "$d/out" \
       --password wrongpw --encrypt >/dev/null 2>&1; rc=$?
if [ $rc -ne 0 ] && ! cmp -s "$d/out/secret.bin" "$TMP/secret.bin" 2>/dev/null; then
  ok "wrong-password rejected (exit $rc)"; else bad "wrong-password not rejected"; fi

# ----------------------------------------------------------------- image BPCS -
log "image BPCS (BMP):"
bpcs_case() { # label, password(may be empty)
  local label="$1"; local pw="$2"
  local d="$TMP/b_$RANDOM"; mkdir -p "$d/out"
  local pwargs=(); [ -n "$pw" ] && pwargs=(--password "$pw")
  "$CLI" bpcs embed --cover "$TMP/cover.bmp" --secret "$TMP/secret.bin" \
         --output "$d/stego.bmp" "${pwargs[@]}" >/dev/null 2>&1
  "$CLI" bpcs extract --stego "$d/stego.bmp" --output "$d/out" \
         "${pwargs[@]}" >/dev/null 2>&1
  if cmp -s "$d/out/secret.bin" "$TMP/secret.bin"; then ok "$label"; else bad "$label"; fi
}
bpcs_case "no password" ""
bpcs_case "with password (encrypt+shuffle)" "pw123"

# -------------------------------------------------------- image LSB (text) ---
log "image LSB text (PNG):"
MSG="StegoNinja text roundtrip 42!"
d="$TMP/lt"; mkdir -p "$d"
"$CLI" image-lsb embed --cover "$TMP/cover.bmp" --message "$MSG" \
       --output "$d/stego.png" >/dev/null 2>&1
OUT=$("$CLI" image-lsb extract --stego "$d/stego.png" 2>/dev/null)
if [ "$OUT" = "$MSG" ]; then ok "text plain"; else bad "text plain"; fi
# encrypted text
"$CLI" image-lsb embed --cover "$TMP/cover.bmp" --message "$MSG" \
       --output "$d/stego_e.png" --encrypt --password k3y >/dev/null 2>&1
OUT=$("$CLI" image-lsb extract --stego "$d/stego_e.png" --encrypt --password k3y 2>/dev/null)
if [ "$OUT" = "$MSG" ]; then ok "text encrypted"; else bad "text encrypted"; fi

# --------------------------------------------------- image LSB (image-in-image)
log "image LSB image-in-image (PNG):"
d="$TMP/li"; mkdir -p "$d"
if "$CLI" image-lsb embed --cover "$TMP/cover.bmp" --secret "$TMP/secret_img.bmp" \
       --output "$d/stego_ii.png" >/dev/null 2>&1 &&
   "$CLI" image-lsb extract --stego "$d/stego_ii.png" --output "$d/recovered.png" >/dev/null 2>&1 &&
   [ -s "$d/recovered.png" ]; then
  ok "image-in-image round-trip"
else
  bad "image-in-image round-trip"
fi

# ------------------------------------------------------------------ video LSB -
log "video LSB (AVI):"
if "$CLI" video --help >/dev/null 2>&1 && command -v ffmpeg >/dev/null; then
  d="$TMP/v"; mkdir -p "$d"
  ffmpeg -loglevel error -f lavfi -i testsrc=duration=1:size=320x240:rate=30 \
         -c:v ffv1 "$d/cover.avi" >/dev/null 2>&1
  MSG="StegoNinja video roundtrip 42"
  video_case(){ # label, embed-extra..., -- , extract-extra...
    local label="$1"; shift
    local emb=(); local ext=()
    while [ "$1" != "--" ]; do emb+=("$1"); shift; done; shift
    ext=("$@")
    local o="$d/$RANDOM"
    "$CLI" video embed --cover "$d/cover.avi" --message "$MSG" --output "$o.avi" "${emb[@]}" >/dev/null 2>&1
    "$CLI" video extract --stego "$o.avi" --output "$o.txt" "${ext[@]}" >/dev/null 2>&1
    if [ -f "$o.txt" ] && [ "$(cat "$o.txt")" = "$MSG" ]; then ok "$label"; else bad "$label"; fi
  }
  video_case "plain (seed 7)" --seed 7 --
  video_case "encrypted (--encrypt)" --seed 3 --encrypt --password vk -- --password vk
  video_case "random frame+pixel" --seed 9 --frame-mode rand --pixel-mode rand --
else
  skip "ffmpeg unavailable"
fi

log ""
log "Summary: $PASS passed, $FAIL failed, $SKIP skipped."
[ "$FAIL" -eq 0 ]
