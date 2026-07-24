#include "engine/imglsb.h"
#include "vigenere.h"

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace steg {
namespace imglsb {

namespace {

// Read the LSB stream in channel-major linear order: bit i lives in the LSB of
// channel (i % 3) of pixel (i / 3), scanning rows then columns.
struct BitReader {
  const cv::Mat &img;
  size_t capacity; // total LSB slots = rows * cols * 3
  explicit BitReader(const cv::Mat &m)
      : img(m), capacity(static_cast<size_t>(m.rows) * m.cols * 3) {}

  int bit(size_t i) const {
    int col = (i / 3) % img.cols;
    int row = i / (static_cast<size_t>(img.cols) * 3);
    int ch = i % 3;
    return img.at<cv::Vec3b>(row, col)[ch] & 1;
  }
};

// Decode `count` bytes starting at bit offset `start`. MSB-first when msbFirst,
// else LSB-first within each byte. Throws if the stream is too short.
std::vector<unsigned char> readBytes(const BitReader &r, size_t start,
                                     size_t count, bool msbFirst) {
  if (start + count * 8 > r.capacity)
    throw std::runtime_error("Stego image too small for payload (corrupt or "
                             "wrong format)");
  std::vector<unsigned char> out(count, 0);
  for (size_t b = 0; b < count; ++b) {
    unsigned char byte = 0;
    for (int k = 0; k < 8; ++k) {
      int val = r.bit(start + b * 8 + k);
      int pos = msbFirst ? (7 - k) : k;
      byte |= (val << pos);
    }
    out[b] = byte;
  }
  return out;
}

cv::Mat readImageOrThrow(const std::string &path, int flags) {
  cv::Mat m = cv::imread(path, flags);
  if (m.empty())
    throw std::runtime_error("Cannot read image: " + path);
  return m;
}

void writeImageOrThrow(const std::string &path, const cv::Mat &m) {
  if (!cv::imwrite(path, m))
    throw std::runtime_error("Failed to write image: " + path);
}

} // namespace

bool previewsEnabled() {
  if (std::getenv("STEGO_NO_PREVIEW") != nullptr)
    return false;
  if (std::getenv("DISPLAY") == nullptr)
    return false;
  return true;
}

Mode detect(const std::string &stegoPath) {
  cv::Mat img = cv::imread(stegoPath, cv::IMREAD_COLOR);
  if (img.empty())
    throw std::runtime_error("Cannot read image: " + stegoPath);
  BitReader r(img);
  if (r.capacity < 32)
    return Mode::None;
  // Same 32 LSBs, two interpretations.
  auto lsbFirst = readBytes(r, 0, 4, /*msbFirst=*/false);
  if (lsbFirst[0] == 'S' && lsbFirst[1] == 'N' && lsbFirst[2] == '1' &&
      lsbFirst[3] == 'M')
    return Mode::Text;
  auto msbFirst = readBytes(r, 0, 4, /*msbFirst=*/true);
  if (msbFirst[0] == 'S' && msbFirst[1] == 'N' && msbFirst[2] == '1' &&
      msbFirst[3] == 'I')
    return Mode::Image;
  return Mode::None;
}

// --- Text -------------------------------------------------------------------

void embedMessage(const std::string &coverPath, const std::string &outPath,
                  const std::string &message, const std::string &key) {
  cv::Mat img = readImageOrThrow(coverPath, cv::IMREAD_COLOR);

  std::string body = key.empty() ? message
                                 : Vigenere::vigenere_encrypt(message, key);

  // Frame: "SN1M" + u32 LE length + body. The length prefix keeps arbitrary
  // (obfuscated) bytes intact where a NUL terminator could not.
  std::vector<unsigned char> payload = {'S', 'N', '1', 'M'};
  uint32_t len = static_cast<uint32_t>(body.size());
  for (int i = 0; i < 4; ++i)
    payload.push_back((len >> (8 * i)) & 0xFF);
  payload.insert(payload.end(), body.begin(), body.end());

  size_t capacity = static_cast<size_t>(img.rows) * img.cols * 3;
  if (payload.size() * 8 > capacity)
    throw std::runtime_error("Message too large for cover capacity");

  // Write LSB-first within each byte.
  size_t bitIndex = 0;
  for (unsigned char byte : payload) {
    for (int k = 0; k < 8; ++k) {
      int col = (bitIndex / 3) % img.cols;
      int row = bitIndex / (static_cast<size_t>(img.cols) * 3);
      int ch = bitIndex % 3;
      uchar &pixel = img.at<cv::Vec3b>(row, col)[ch];
      pixel = (pixel & 0xFE) | ((byte >> k) & 1);
      ++bitIndex;
    }
  }

  writeImageOrThrow(outPath, img);
}

std::string extractMessage(const std::string &stegoPath,
                           const std::string &key) {
  cv::Mat img = readImageOrThrow(stegoPath, cv::IMREAD_COLOR);
  BitReader r(img);

  auto magic = readBytes(r, 0, 4, /*msbFirst=*/false);
  if (!(magic[0] == 'S' && magic[1] == 'N' && magic[2] == '1' &&
        magic[3] == 'M'))
    throw std::runtime_error("Not a StegoNinja text-LSB stego (bad magic)");

  auto lenBytes = readBytes(r, 32, 4, /*msbFirst=*/false);
  uint32_t len = 0;
  for (int i = 0; i < 4; ++i)
    len |= static_cast<uint32_t>(lenBytes[i]) << (8 * i);

  auto body = readBytes(r, 64, len, /*msbFirst=*/false);
  std::string msg(body.begin(), body.end());
  if (!key.empty())
    msg = Vigenere::vigenere_decrypt(msg, key);
  return msg;
}

// --- Image-in-image ---------------------------------------------------------

void embedImage(const std::string &coverPath, const std::string &secretPath,
                const std::string &outPath, const std::string &key) {
  cv::Mat carrier = readImageOrThrow(coverPath, cv::IMREAD_COLOR);
  cv::Mat secret = readImageOrThrow(secretPath, cv::IMREAD_COLOR);

  std::vector<unsigned char> secretBuffer;
  if (!cv::imencode(".png", secret, secretBuffer))
    throw std::runtime_error("Failed to PNG-encode secret image");

  if (!key.empty())
    secretBuffer = Vigenere::vigenereEncrypt(secretBuffer, key);

  size_t dataSize = secretBuffer.size();
  size_t totalBits = (4 * 8) + (8 * 8) + (dataSize * 8); // magic + header + data
  size_t capacity = static_cast<size_t>(carrier.rows) * carrier.cols * 3;
  if (totalBits > capacity)
    throw std::runtime_error("Secret image too large for cover capacity");

  // Payload: "SN1I" + u64 LE length + data, embedded MSB-first.
  std::vector<unsigned char> payload = {'S', 'N', '1', 'I'};
  for (int i = 0; i < 8; ++i)
    payload.push_back((static_cast<uint64_t>(dataSize) >> (8 * i)) & 0xFF);
  payload.insert(payload.end(), secretBuffer.begin(), secretBuffer.end());

  size_t bitIndex = 0;
  for (int row = 0; row < carrier.rows && bitIndex < totalBits; ++row) {
    for (int col = 0; col < carrier.cols && bitIndex < totalBits; ++col) {
      cv::Vec3b &pixel = carrier.at<cv::Vec3b>(row, col);
      for (int channel = 0; channel < 3 && bitIndex < totalBits; ++channel) {
        size_t byteIndex = bitIndex / 8;
        int bitPosition = 7 - (bitIndex % 8);
        int bit = (payload[byteIndex] >> bitPosition) & 1;
        pixel[channel] = (pixel[channel] & 0xFE) | bit;
        ++bitIndex;
      }
    }
  }

  writeImageOrThrow(outPath, carrier);
}

void extractImage(const std::string &stegoPath, const std::string &outPath,
                  const std::string &key) {
  cv::Mat img = readImageOrThrow(stegoPath, cv::IMREAD_COLOR);
  BitReader r(img);

  auto magic = readBytes(r, 0, 4, /*msbFirst=*/true);
  if (!(magic[0] == 'S' && magic[1] == 'N' && magic[2] == '1' &&
        magic[3] == 'I'))
    throw std::runtime_error(
        "Not a StegoNinja image-in-image stego (bad magic)");

  auto sizeBuffer = readBytes(r, 32, 8, /*msbFirst=*/true);
  uint64_t dataSize = 0;
  for (int i = 0; i < 8; ++i)
    dataSize |= static_cast<uint64_t>(sizeBuffer[i]) << (8 * i);

  auto encryptedData = readBytes(r, 96, dataSize, /*msbFirst=*/true);
  if (!key.empty())
    encryptedData = Vigenere::vigenereDecrypt(encryptedData, key);

  cv::Mat extracted = cv::imdecode(encryptedData, cv::IMREAD_UNCHANGED);
  if (extracted.empty())
    throw std::runtime_error(
        "Failed to decode hidden image (wrong key or corrupt stego)");

  writeImageOrThrow(outPath, extracted);
}

// --- Quality feedback -------------------------------------------------------

double psnr(const cv::Mat &a, const cv::Mat &b) {
  if (a.empty() || b.empty())
    throw std::runtime_error("PSNR: empty image");
  if (a.size() != b.size() || a.type() != b.type())
    throw std::runtime_error("PSNR: image size/type mismatch");
  cv::Mat diff;
  cv::absdiff(a, b, diff);
  diff.convertTo(diff, CV_32F);
  diff = diff.mul(diff);
  cv::Scalar s = cv::sum(diff);
  double sse = s.val[0] + s.val[1] + s.val[2];
  if (sse <= 1e-10)
    return INFINITY;
  double mse = sse / (static_cast<double>(a.channels()) * a.total());
  return 10.0 * std::log10((255.0 * 255.0) / mse);
}

double psnrFiles(const std::string &aPath, const std::string &bPath) {
  return psnr(readImageOrThrow(aPath, cv::IMREAD_COLOR),
              readImageOrThrow(bPath, cv::IMREAD_COLOR));
}

} // namespace imglsb
} // namespace steg
