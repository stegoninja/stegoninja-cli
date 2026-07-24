#ifndef STEGONINJA_ENGINE_VIDEOLSB_H
#define STEGONINJA_ENGINE_VIDEOLSB_H

#include <string>

// Video LSB engine. A single-line text message rides in frame LSBs of a
// lossless FFV1/AVI stego video. Frame 0 stores metadata (mode flags + 5-bit
// seed) and the message bit-length; frames 1.. carry the message. Pure logic —
// no stdin/stdout. Errors throw std::runtime_error.
namespace steg {
namespace videolsb {

struct EmbedOptions {
  int seed = 0;             // 0..31, stored in-band, drives the RNG
  bool frameRandom = false; // shuffle frame processing order
  bool pixelRandom = false; // shuffle pixel order within a frame
  std::string key;          // non-empty => Vigenere obfuscation
};

// Embed `message` into cover video `coverPath`, writing FFV1/AVI `outPath`.
void embed(const std::string &coverPath, const std::string &outPath,
           const std::string &message, const EmbedOptions &opts);

// Extract and return the hidden message from stego video `stegoPath`. If the
// stego was encrypted, `key` is used to de-obfuscate (wrong/empty key yields
// garbled text, never a crash).
std::string extract(const std::string &stegoPath, const std::string &key = "");

// Average PSNR (dB) across frames of two videos; +inf if identical.
double psnr(const std::string &aPath, const std::string &bPath);

} // namespace videolsb
} // namespace steg

#endif // STEGONINJA_ENGINE_VIDEOLSB_H
