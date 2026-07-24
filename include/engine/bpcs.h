#ifndef STEGONINJA_ENGINE_BPCS_H
#define STEGONINJA_ENGINE_BPCS_H

#include <string>

// Image BPCS engine over 24-bit BMP covers. Data rides in 8x8 bit-plane blocks
// whose complexity (adjacent-bit transition count) meets the threshold (34).
// Pure logic — no OpenCV, no stdin/stdout. Errors throw std::runtime_error.
//
// NOTE (option handling differs from the LSB tools, by design): a non-empty
// password enables BOTH Vigenere obfuscation AND a password-seeded block
// shuffle; an empty password does neither. Extract must use the same password.
//
// KNOWN LIMITATION: this is a simplified BPCS without a conjugation map. Extract
// recomputes the eligible-block set from the STEGO planes, so a low-complexity
// payload chunk written into an eligible block can drop that block below the
// complexity threshold and desync embed/extract. High-entropy payloads (e.g.
// compressed/encrypted files) keep complexity high and round-trip reliably;
// adding a conjugation map to guarantee it for all payloads is future work.
namespace steg {
namespace bpcs {

// Embed `secretPath` into `coverPath` (BMP), writing `outPath` (BMP).
// Returns the embedding PSNR in dB (+inf if identical).
double embed(const std::string &coverPath, const std::string &secretPath,
             const std::string &outPath, const std::string &password);

// Extract the hidden file from `stegoPath` (BMP) into directory `outDir`,
// recreating the stored filename. Returns the full path written.
std::string extract(const std::string &stegoPath, const std::string &outDir,
                    const std::string &password);

} // namespace bpcs
} // namespace steg

#endif // STEGONINJA_ENGINE_BPCS_H
