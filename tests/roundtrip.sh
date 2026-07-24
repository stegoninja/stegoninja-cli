#!/usr/bin/env bash
# Round-trip test matrix for StegoNinja CLI tools.
#
# Verifies embed -> extract recovers the secret byte-for-byte for every
# technique x option combination that is scriptable. Audio and BPCS are always
# tested. Video is tested only when a non-interactive SteganoVid binary exists.
# The image LSB tool (SteganoImgLsb) is an interactive ncurses TUI and cannot be
# driven headlessly, so it is out of scope for this harness.
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

AUDIO="$BUILD/audio"
BPCE="$BUILD/imgBPCSEmbed"
BPCX="$BUILD/imgBPCSExtract"
VID="$BUILD/SteganoVid"

# ---------------------------------------------------------------- audio LSB ---
log "audio LSB (WAV):"
if [ -x "$AUDIO" ]; then
  audio_case() { # label, flags...
    local label="$1"; shift
    local d="$TMP/a_$RANDOM"; mkdir -p "$d/out"
    "$AUDIO" embed "$TMP/cover.wav" "$TMP/secret.bin" "$d/stego.wav" pw "$@" >/dev/null 2>&1
    ( cd "$d/out" && "$AUDIO" extract "$d/stego.wav" pw "$@" >/dev/null 2>&1 )
    if cmp -s "$d/out/secret.bin" "$TMP/secret.bin"; then ok "$label"; else bad "$label"; fi
  }
  audio_case "plain"
  audio_case "encrypt (-e)" -e
  audio_case "randomize (-r)" -r
  audio_case "encrypt+randomize (-e -r)" -e -r
  # negative: wrong password must fail cleanly (non-zero), not crash, not recover
  d="$TMP/a_neg"; mkdir -p "$d/out"
  "$AUDIO" embed "$TMP/cover.wav" "$TMP/secret.bin" "$d/stego.wav" rightpw -e >/dev/null 2>&1
  ( cd "$d/out" && "$AUDIO" extract "$d/stego.wav" wrongpw -e >/dev/null 2>&1 ); rc=$?
  if [ $rc -ne 0 ] && ! cmp -s "$d/out/secret.bin" "$TMP/secret.bin" 2>/dev/null; then
    ok "wrong-password rejected (exit $rc)"; else bad "wrong-password not rejected"; fi
else
  skip "audio binary not built"
fi

# ----------------------------------------------------------------- image BPCS -
log "image BPCS (BMP):"
if [ -x "$BPCE" ] && [ -x "$BPCX" ]; then
  bpcs_case() { # label, password(may be empty)
    local label="$1"; local pw="$2"
    local d="$TMP/b_$RANDOM"; mkdir -p "$d/out"
    printf '%s\n' "$pw" | "$BPCE" "$TMP/cover.bmp" "$TMP/secret.bin" "$d/stego.bmp" >/dev/null 2>&1
    printf '%s\n' "$pw" | "$BPCX" "$d/stego.bmp" "$d/out" >/dev/null 2>&1
    if cmp -s "$d/out/secret.bin" "$TMP/secret.bin"; then ok "$label"; else bad "$label"; fi
  }
  bpcs_case "no password" ""
  bpcs_case "with password (encrypt+randomize)" "pw123"
else
  skip "BPCS binaries not built"
fi

# ------------------------------------------------------------------ video LSB -
log "video LSB (AVI):"
if [ -x "$VID" ] && "$VID" --help >/dev/null 2>&1; then
  d="$TMP/v"; mkdir -p "$d/out"
  if "$VID" embed --cover "$TMP/cover.bmp" --secret "$TMP/secret.bin" \
        --output "$d/stego.avi" --seed 7 >/dev/null 2>&1 \
     && "$VID" extract --stego "$d/stego.avi" --seed 7 --output "$d/out/secret.bin" >/dev/null 2>&1 \
     && cmp -s "$d/out/secret.bin" "$TMP/secret.bin"; then
    ok "video plain (seed 7)"
  else
    bad "video plain"
  fi
else
  skip "SteganoVid non-interactive mode unavailable (needs OpenCV build + flags)"
fi

log ""
log "Summary: $PASS passed, $FAIL failed, $SKIP skipped."
[ "$FAIL" -eq 0 ]
