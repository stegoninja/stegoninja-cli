#ifndef STEGONINJA_ENGINE_H
#define STEGONINJA_ENGINE_H

// Umbrella header for the StegoNinja steganography engine — the single shared
// unit that both front-ends (the `stegoninja` CLI and the `stegoninja-tui`)
// link. All technique + format logic lives here; the front-ends only gather
// inputs and render results.
#include "engine/audiolsb.h"
#include "engine/bpcs.h"
#include "engine/imglsb.h"
#include "engine/videolsb.h"
#include "vigenere.h"

#endif // STEGONINJA_ENGINE_H
