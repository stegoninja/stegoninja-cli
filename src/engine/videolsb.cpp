#include "engine/videolsb.h"
#include "vigenere.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <opencv2/opencv.hpp>
#include <random>
#include <stdexcept>
#include <vector>

namespace steg {
namespace videolsb {

namespace {

std::vector<bool> messageToBits(const std::string &message) {
  std::vector<bool> bits;
  for (char c : message)
    for (int i = 7; i >= 0; --i)
      bits.push_back((c >> i) & 1);
  return bits;
}

std::string bitsToMessage(const std::vector<bool> &bits) {
  std::string message;
  for (size_t i = 0; i < bits.size(); i += 8) {
    char c = 0;
    for (int j = 0; j < 8; ++j)
      if (i + j < bits.size())
        c |= bits[i + j] << (7 - j);
    message += c;
  }
  return message;
}

void embedMetadata(cv::Mat &frame, uint8_t metadata) {
  if (frame.cols < 8)
    throw std::runtime_error("Frame not wide enough to embed metadata");
  for (int p = 0; p < 8; ++p) {
    cv::Vec3b &pixel = frame.at<cv::Vec3b>(0, p);
    pixel[0] = (pixel[0] & 0xFE) | ((metadata >> p) & 1); // little-endian
  }
}

uint8_t extractMetadata(const cv::Mat &frame) {
  if (frame.cols < 8)
    throw std::runtime_error("Frame too small to contain metadata");
  uint8_t metadata = 0;
  for (int p = 0; p < 8; ++p)
    metadata |= (frame.at<cv::Vec3b>(0, p)[0] & 1) << p;
  return metadata;
}

uint32_t extractLength(const cv::Mat &frame) {
  uint32_t lengthBits = 0;
  for (int p = 0; p < 32; ++p) {
    int pixelIdx = p + 8;
    int x = pixelIdx % frame.cols;
    int y = pixelIdx / frame.cols;
    if (y >= frame.rows)
      break;
    lengthBits |= (frame.at<cv::Vec3b>(y, x)[0] & 1) << (31 - p);
  }
  return lengthBits;
}

void embedBitsInFrame(cv::Mat &frame, const std::vector<bool> &bits,
                      size_t &bitIndex, bool randomPixels, std::mt19937 &rng) {
  std::vector<std::pair<int, int>> pixelIndices;
  for (int y = 0; y < frame.rows; ++y)
    for (int x = 0; x < frame.cols; ++x)
      pixelIndices.push_back({y, x});
  if (randomPixels)
    std::shuffle(pixelIndices.begin(), pixelIndices.end(), rng);

  for (auto [y, x] : pixelIndices) {
    cv::Vec3b &pixel = frame.at<cv::Vec3b>(y, x);
    for (int c = 0; c < 3; ++c) {
      if (bitIndex >= bits.size())
        return;
      pixel[c] = (pixel[c] & ~1) | bits[bitIndex++];
    }
  }
}

} // namespace

void embed(const std::string &coverPath, const std::string &outPath,
           const std::string &message, const EmbedOptions &opts) {
  if (opts.seed < 0 || opts.seed > 31)
    throw std::runtime_error("seed must be in the range 0..31");

  std::string body = opts.key.empty()
                         ? message
                         : Vigenere::vigenere_encrypt(message, opts.key);
  body += '\0'; // terminator, appended after obfuscation

  std::vector<bool> messageBits = messageToBits(body);
  uint32_t messageBitLength = static_cast<uint32_t>(messageBits.size());

  cv::VideoCapture cap(coverPath);
  if (!cap.isOpened())
    throw std::runtime_error("Failed to open input video: " + coverPath);

  int frameWidth = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
  int frameHeight = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
  double fps = cap.get(cv::CAP_PROP_FPS);
  int totalFrames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));

  cv::VideoWriter writer;
  int codec = cv::VideoWriter::fourcc('F', 'F', 'V', '1');
  writer.open(outPath, codec, fps, cv::Size(frameWidth, frameHeight), true);
  if (!writer.isOpened())
    throw std::runtime_error("Failed to open output video (FFV1 writer): " +
                             outPath);

  uint8_t metadata = (opts.frameRandom << 7) | (opts.pixelRandom << 6) |
                     (!opts.key.empty() << 5);
  metadata |= (opts.seed & 0x1F);

  size_t bitIndex = 0;
  std::mt19937 rng(opts.seed);

  // Frame order: shuffling advances the shared RNG (keeping per-frame pixel
  // shuffles in sync) but the message still lands in output frames 1,2,3,...
  std::vector<int> frameIndices(totalFrames);
  std::iota(frameIndices.begin(), frameIndices.end(), 0);
  if (opts.frameRandom && totalFrames > 1)
    std::shuffle(frameIndices.begin() + 1, frameIndices.end(), rng);

  // Length bits stored in-band in frame 0.
  std::vector<bool> lengthBits;
  for (int i = 31; i >= 0; --i)
    lengthBits.push_back((messageBitLength >> i) & 1);

  bool embedded = false;
  for (int i = 0; i < totalFrames; ++i) {
    int frameNumber = opts.frameRandom ? frameIndices[i] : i;
    cap.set(cv::CAP_PROP_POS_FRAMES, frameNumber);
    cv::Mat frame;
    if (!cap.read(frame) || frame.empty())
      break;

    if (frameNumber == 0) {
      embedMetadata(frame, metadata);
      for (int p = 0; p < 32; ++p) {
        int pixelIdx = p + 8;
        int x = pixelIdx % frame.cols;
        int y = pixelIdx / frame.cols;
        if (y >= frame.rows)
          break;
        frame.at<cv::Vec3b>(y, x)[0] =
            (frame.at<cv::Vec3b>(y, x)[0] & ~1) | lengthBits[p];
      }
    } else {
      embedBitsInFrame(frame, messageBits, bitIndex, opts.pixelRandom, rng);
    }
    writer.write(frame);

    if (bitIndex >= messageBits.size()) {
      embedded = true;
      for (int j = i + 1; j < totalFrames; ++j) {
        cap.set(cv::CAP_PROP_POS_FRAMES,
                opts.frameRandom ? frameIndices[j] : j);
        cv::Mat rest;
        if (!cap.read(rest) || rest.empty())
          break;
        writer.write(rest);
      }
      break;
    }
  }

  cap.release();
  writer.release();
  if (!embedded && bitIndex < messageBits.size())
    throw std::runtime_error("Not enough capacity to embed the entire message");
}

std::string extract(const std::string &stegoPath, const std::string &key) {
  cv::VideoCapture cap(stegoPath);
  if (!cap.isOpened())
    throw std::runtime_error("Failed to open video: " + stegoPath);

  int totalFrames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
  cv::Mat frame;
  if (!cap.read(frame) || frame.empty())
    throw std::runtime_error("Failed to read first frame");

  uint8_t metadata = extractMetadata(frame);
  uint32_t messageLengthBits = extractLength(frame);
  bool frameMode = (metadata >> 7) & 1;
  bool pixelMode = (metadata >> 6) & 1;
  bool encryptFlag = (metadata >> 5) & 1;
  uint32_t seed = metadata & 0x1F;

  std::mt19937 rng(seed);
  std::vector<int> frameIndices(totalFrames);
  std::iota(frameIndices.begin(), frameIndices.end(), 0);
  if (frameMode && totalFrames > 1)
    std::shuffle(frameIndices.begin() + 1, frameIndices.end(), rng);

  // Message lands in stego frames 1,2,3,... in order (see embed); read
  // sequentially. The frame shuffle above only advances the shared RNG so the
  // per-frame pixel shuffle stays in sync.
  std::vector<bool> bits;
  int remaining = static_cast<int>(messageLengthBits);
  for (int i = 1; i < totalFrames && remaining > 0; ++i) {
    cap.set(cv::CAP_PROP_POS_FRAMES, i);
    cv::Mat f;
    if (!cap.read(f) || f.empty())
      break;
    std::vector<std::pair<int, int>> pixelIndices;
    for (int y = 0; y < f.rows; ++y)
      for (int x = 0; x < f.cols; ++x)
        pixelIndices.push_back({y, x});
    if (pixelMode)
      std::shuffle(pixelIndices.begin(), pixelIndices.end(), rng);
    for (auto [y, x] : pixelIndices) {
      cv::Vec3b pixel = f.at<cv::Vec3b>(y, x);
      for (int c = 0; c < 3; ++c) {
        if (remaining <= 0)
          break;
        bits.push_back(pixel[c] & 1);
        --remaining;
      }
      if (remaining <= 0)
        break;
    }
  }
  cap.release();

  std::string decoded = bitsToMessage(bits);
  // Drop the trailing NUL terminator (appended after obfuscation at embed),
  // then de-obfuscate the remaining bytes.
  if (!decoded.empty() && decoded.back() == '\0')
    decoded.pop_back();
  if (encryptFlag) {
    if (key.empty())
      throw std::runtime_error(
          "This stego video is encrypted; a key is required to extract it");
    decoded = Vigenere::vigenere_decrypt(decoded, key);
  }
  return decoded;
}

double psnr(const std::string &aPath, const std::string &bPath) {
  cv::VideoCapture a(aPath), b(bPath);
  if (!a.isOpened() || !b.isOpened())
    throw std::runtime_error("Failed to open one of the videos for PSNR");

  int totalFrames = std::min((int)a.get(cv::CAP_PROP_FRAME_COUNT),
                             (int)b.get(cv::CAP_PROP_FRAME_COUNT));
  double total = 0.0;
  int counted = 0;
  for (int i = 0; i < totalFrames; ++i) {
    cv::Mat fa, fb;
    a >> fa;
    b >> fb;
    if (fa.empty() || fb.empty())
      break;
    if (fa.size() != fb.size() || fa.type() != fb.type())
      continue;
    cv::Mat diff;
    cv::absdiff(fa, fb, diff);
    diff.convertTo(diff, CV_32F);
    diff = diff.mul(diff);
    cv::Scalar s = cv::sum(diff);
    double sse = s.val[0] + s.val[1] + s.val[2];
    if (sse <= 1e-10)
      continue;
    double mse = sse / (fa.channels() * fa.total());
    total += 10.0 * std::log10((255.0 * 255.0) / mse);
    ++counted;
  }
  return counted == 0 ? INFINITY : total / counted;
}

} // namespace videolsb
} // namespace steg
