#ifndef STEGONINJA_ENGINE_AUDIOLSB_H
#define STEGONINJA_ENGINE_AUDIOLSB_H

#include <string>

// Audio LSB engine over PCM WAV covers. An arbitrary secret file (plus its
// filename) rides in the LSBs of the data chunk's bytes. Pure logic — no
// stdin/stdout. Errors throw std::runtime_error.
//
// Options: `encrypt` Vigenere-obfuscates the header+payload with `password`;
// `randomize` shuffles sample positions using a password-seeded RNG. Extract
// must use the SAME password and flags.
namespace steg {
namespace audiolsb {

// Embed `secretPath` into WAV `coverPath`, writing WAV `outPath`.
// Returns the embedding PSNR in dB (+inf if unchanged).
double embed(const std::string &coverPath, const std::string &secretPath,
             const std::string &outPath, const std::string &password,
             bool encrypt, bool randomize);

// Extract the hidden file from WAV `stegoPath` into directory `outDir`,
// recreating the stored filename. Returns the full path written.
std::string extract(const std::string &stegoPath, const std::string &outDir,
                    const std::string &password, bool encrypt, bool randomize);

} // namespace audiolsb
} // namespace steg

#endif // STEGONINJA_ENGINE_AUDIOLSB_H
