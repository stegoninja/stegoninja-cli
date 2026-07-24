#ifndef STEGONINJA_ENGINE_IMGLSB_H
#define STEGONINJA_ENGINE_IMGLSB_H

#include <opencv2/opencv.hpp>
#include <string>

// Image LSB engine: text ("Message") and image-in-image ("Image").
// Pure logic with no ncurses / stdin / stdout — both front-ends call these and
// render their own UI. Errors are signalled with std::runtime_error.
namespace steg {
namespace imglsb {

// Which StegoNinja LSB payload (if any) a stego image carries. The two formats
// are NOT cross-decodable: text is framed "SN1M" LSB-first, image-in-image is
// framed "SN1I" MSB-first, so the magic + bit order disambiguate them.
enum class Mode { None, Text, Image };

// Inspect the first bytes of the LSB stream and report which format is present.
Mode detect(const std::string &stegoPath);

// --- Text ("Message") -------------------------------------------------------
// Payload = ["SN1M"][u32 LE length][message bytes]. The length prefix (instead
// of a NUL terminator) lets an optionally Vigenere-obfuscated message contain
// any byte, including 0x00. Pass an empty key for no obfuscation.
void embedMessage(const std::string &coverPath, const std::string &outPath,
                  const std::string &message, const std::string &key = "");
std::string extractMessage(const std::string &stegoPath,
                           const std::string &key = "");

// --- Image-in-image ("Image") ----------------------------------------------
// Payload = ["SN1I"][u64 LE length][PNG-encoded secret], MSB-first. Optional
// Vigenere obfuscation of the PNG bytes when key is non-empty.
void embedImage(const std::string &coverPath, const std::string &secretPath,
                const std::string &outPath, const std::string &key = "");
void extractImage(const std::string &stegoPath, const std::string &outPath,
                  const std::string &key = "");

// --- Quality feedback -------------------------------------------------------
// Peak signal-to-noise ratio (dB) between two equally sized images; returns
// +inf when identical. Throws if either file cannot be read or sizes differ.
double psnr(const cv::Mat &a, const cv::Mat &b);
double psnrFiles(const std::string &aPath, const std::string &bPath);

// True when OpenCV preview windows should be shown. Disabled when
// STEGO_NO_PREVIEW is set or no DISPLAY is available, so callers can run
// headless (e.g. over SSH) without trying to open a window.
bool previewsEnabled();

} // namespace imglsb
} // namespace steg

#endif // STEGONINJA_ENGINE_IMGLSB_H
