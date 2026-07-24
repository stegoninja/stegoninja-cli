#include "engine/bpcs.h"
#include "vigenere.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <vector>

namespace steg {
namespace bpcs {

namespace {

#pragma pack(push, 1)
struct BMPFileHeader {
  uint16_t fileType{0x4D42};
  uint32_t fileSize{0};
  uint16_t reserved1{0};
  uint16_t reserved2{0};
  uint32_t offsetData{0};
};
struct BMPInfoHeader {
  uint32_t headerSize{40};
  int32_t width{0};
  int32_t height{0};
  uint16_t planes{1};
  uint16_t bitCount{24};
  uint32_t compression{0};
  uint32_t imageSize{0};
  int32_t xPixelsPerMeter{2835};
  int32_t yPixelsPerMeter{2835};
  uint32_t colorsUsed{0};
  uint32_t colorsImportant{0};
};
#pragma pack(pop)

struct RGB {
  uint8_t r, g, b;
};
struct BlockPosition {
  int channel, bitPlane, x, y;
};

const int kThreshold = 34;

std::vector<RGB> readBMP(const std::string &filename, int &width, int &height) {
  std::ifstream file(filename, std::ios::binary);
  if (!file)
    throw std::runtime_error("Failed to open BMP file: " + filename);

  BMPFileHeader fileHeader;
  BMPInfoHeader infoHeader;
  file.read(reinterpret_cast<char *>(&fileHeader), sizeof(fileHeader));
  file.read(reinterpret_cast<char *>(&infoHeader), sizeof(infoHeader));
  if (!file)
    throw std::runtime_error("Truncated BMP header: " + filename);

  if (fileHeader.fileType != 0x4D42 || infoHeader.headerSize != 40 ||
      infoHeader.bitCount != 24 || infoHeader.compression != 0)
    throw std::runtime_error("Unsupported BMP format (need 24-bit uncompressed)");

  // Dimensions come from the (untrusted) header. Validate sign and magnitude in
  // 64-bit before any 32-bit size math, so a hostile BMP cannot overflow
  // `rowSize`/`dataSize`/`width*height` into a small allocation + OOB access.
  if (infoHeader.width <= 0 || infoHeader.height == 0)
    throw std::runtime_error("Invalid BMP dimensions");
  width = infoHeader.width;
  height = std::abs(infoHeader.height);
  const uint64_t kMaxPixels = 200ull * 1000 * 1000; // generous sane cap
  uint64_t rowSize64 = ((static_cast<uint64_t>(width) * 3) + 3) & ~3ull;
  uint64_t dataSize64 = rowSize64 * static_cast<uint64_t>(height);
  if (static_cast<uint64_t>(width) * height > kMaxPixels ||
      dataSize64 > 4 * kMaxPixels)
    throw std::runtime_error("BMP too large");

  int rowSize = static_cast<int>(rowSize64);
  int dataSize = static_cast<int>(dataSize64);

  std::vector<uint8_t> data(dataSize);
  file.seekg(fileHeader.offsetData);
  file.read(reinterpret_cast<char *>(data.data()), dataSize);
  if (file.gcount() != dataSize)
    throw std::runtime_error("Truncated BMP pixel data: " + filename);

  std::vector<RGB> pixels(width * height);
  bool isBottomUp = infoHeader.height > 0;
  for (int y = 0; y < height; ++y) {
    int srcY = isBottomUp ? height - 1 - y : y;
    const uint8_t *row = data.data() + srcY * rowSize;
    for (int x = 0; x < width; ++x) {
      pixels[y * width + x].r = row[x * 3 + 2];
      pixels[y * width + x].g = row[x * 3 + 1];
      pixels[y * width + x].b = row[x * 3 + 0];
    }
  }
  return pixels;
}

void writeBMP(const std::string &filename, const std::vector<RGB> &pixels,
              int width, int height) {
  std::ofstream file(filename, std::ios::binary);
  if (!file)
    throw std::runtime_error("Failed to create BMP file: " + filename);

  int rowSize = (width * 3 + 3) & ~3;
  int dataSize = rowSize * height;
  int fileSize = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + dataSize;

  BMPFileHeader fileHeader;
  fileHeader.fileSize = fileSize;
  fileHeader.offsetData = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader);

  BMPInfoHeader infoHeader;
  infoHeader.width = width;
  infoHeader.height = -height;

  file.write(reinterpret_cast<const char *>(&fileHeader), sizeof(fileHeader));
  file.write(reinterpret_cast<const char *>(&infoHeader), sizeof(infoHeader));

  std::vector<uint8_t> row(rowSize);
  for (int y = 0; y < height; ++y) {
    const RGB *srcRow = pixels.data() + y * width;
    for (int x = 0; x < width; ++x) {
      row[x * 3 + 0] = srcRow[x].b;
      row[x * 3 + 1] = srcRow[x].g;
      row[x * 3 + 2] = srcRow[x].r;
    }
    std::fill(row.begin() + width * 3, row.end(), 0);
    file.write(reinterpret_cast<const char *>(row.data()), rowSize);
  }
}

std::vector<uint8_t> readFile(const std::string &filename) {
  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file)
    throw std::runtime_error("Failed to open file: " + filename);
  std::streamsize size = file.tellg();
  if (size < 0)
    throw std::runtime_error("Failed to size file: " + filename);
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(static_cast<size_t>(size));
  if (size > 0 && !file.read(reinterpret_cast<char *>(buffer.data()), size))
    throw std::runtime_error("Failed to read file: " + filename);
  return buffer;
}

uint8_t &channelRef(RGB &p, int channel) {
  return channel == 0 ? p.r : (channel == 1 ? p.g : p.b);
}
uint8_t channelVal(const RGB &p, int channel) {
  return channel == 0 ? p.r : (channel == 1 ? p.g : p.b);
}

// Collect, in a fixed scan order, every 8x8 bit-plane block whose complexity
// meets the threshold. Embed and extract must build this list identically.
std::vector<BlockPosition> eligibleBlocks(const std::vector<RGB> &pixels,
                                          int width, int height) {
  std::vector<BlockPosition> blocks;
  for (int channel = 0; channel < 3; ++channel)
    for (int bitPlane = 7; bitPlane >= 0; --bitPlane)
      for (int y = 0; y + 8 <= height; y += 8)
        for (int x = 0; x + 8 <= width; x += 8) {
          int complexity = 0;
          bool prevRow[8];
          for (int i = 0; i < 8; ++i) {
            for (int j = 0; j < 8; ++j) {
              int idx = (y + i) * width + (x + j);
              bool bit = (channelVal(pixels[idx], channel) >> (7 - bitPlane)) & 1;
              if (j < 7) {
                int idxR = (y + i) * width + (x + j + 1);
                bool bitR =
                    (channelVal(pixels[idxR], channel) >> (7 - bitPlane)) & 1;
                if (bit != bitR)
                  complexity++;
              }
              if (i > 0 && bit != prevRow[j])
                complexity++;
              prevRow[j] = bit;
            }
          }
          if (complexity >= kThreshold)
            blocks.push_back({channel, bitPlane, x, y});
        }
  return blocks;
}

unsigned int passwordSeed(const std::string &password) {
  unsigned int seed = 0;
  for (char c : password)
    seed += static_cast<unsigned int>(c);
  return seed;
}

} // namespace

double embed(const std::string &coverPath, const std::string &secretPath,
             const std::string &outPath, const std::string &password) {
  int width, height;
  std::vector<RGB> originalPixels = readBMP(coverPath, width, height);
  std::vector<RGB> pixels = originalPixels;

  std::vector<uint8_t> secretData = readFile(secretPath);
  std::string secretFilename =
      std::filesystem::path(secretPath).filename().string();
  if (secretFilename.size() > 255)
    throw std::runtime_error("Filename exceeds 255 characters");

  bool usePassword = !password.empty();

  // Header: [u8 filename_len][filename][u32 LE secret_len][secret].
  std::vector<uint8_t> dataToEmbed;
  dataToEmbed.push_back(static_cast<uint8_t>(secretFilename.size()));
  dataToEmbed.insert(dataToEmbed.end(), secretFilename.begin(),
                     secretFilename.end());
  uint32_t secretLength = static_cast<uint32_t>(secretData.size());
  dataToEmbed.insert(dataToEmbed.end(), reinterpret_cast<uint8_t *>(&secretLength),
                     reinterpret_cast<uint8_t *>(&secretLength) + 4);
  dataToEmbed.insert(dataToEmbed.end(), secretData.begin(), secretData.end());

  if (usePassword)
    dataToEmbed = Vigenere::vigenereEncrypt(dataToEmbed, password);

  std::vector<bool> bits;
  bits.reserve(dataToEmbed.size() * 8);
  for (uint8_t byte : dataToEmbed)
    for (int i = 7; i >= 0; --i)
      bits.push_back((byte >> i) & 1);

  std::vector<BlockPosition> blocks = eligibleBlocks(pixels, width, height);
  if (bits.size() > blocks.size() * 64)
    throw std::runtime_error("Secret too large. Max capacity: " +
                             std::to_string(blocks.size() * 64 / 8) + " bytes");

  if (usePassword)
    std::shuffle(blocks.begin(), blocks.end(),
                 std::default_random_engine(passwordSeed(password)));

  size_t bitIndex = 0;
  for (const auto &pos : blocks) {
    if (bitIndex >= bits.size())
      break;
    for (int i = 0; i < 8 && bitIndex < bits.size(); ++i)
      for (int j = 0; j < 8 && bitIndex < bits.size(); ++j) {
        int idx = (pos.y + i) * width + (pos.x + j);
        uint8_t &value = channelRef(pixels[idx], pos.channel);
        value &= ~(1 << (7 - pos.bitPlane));
        value |= (bits[bitIndex++] << (7 - pos.bitPlane));
      }
  }

  double sum = 0.0;
  for (size_t i = 0; i < pixels.size(); ++i) {
    sum += std::pow(originalPixels[i].r - pixels[i].r, 2);
    sum += std::pow(originalPixels[i].g - pixels[i].g, 2);
    sum += std::pow(originalPixels[i].b - pixels[i].b, 2);
  }
  double mse = sum / (3.0 * width * height);

  writeBMP(outPath, pixels, width, height);
  return mse == 0 ? INFINITY : 20.0 * std::log10(256.0 / std::sqrt(mse));
}

std::string extract(const std::string &stegoPath, const std::string &outDir,
                    const std::string &password) {
  int width, height;
  std::vector<RGB> pixels = readBMP(stegoPath, width, height);
  bool usePassword = !password.empty();

  std::vector<BlockPosition> blocks = eligibleBlocks(pixels, width, height);
  if (usePassword)
    std::shuffle(blocks.begin(), blocks.end(),
                 std::default_random_engine(passwordSeed(password)));

  std::vector<uint8_t> data;
  data.reserve(blocks.size() * 8);
  for (const auto &pos : blocks) {
    for (int i = 0; i < 8; ++i) {
      uint8_t byte = 0;
      for (int j = 0; j < 8; ++j) {
        int idx = (pos.y + i) * width + (pos.x + j);
        byte |= (((channelVal(pixels[idx], pos.channel) >> (7 - pos.bitPlane)) &
                  1)
                 << (7 - j));
      }
      data.push_back(byte);
    }
  }

  if (usePassword)
    data = Vigenere::vigenereDecrypt(data, password);

  const char *corrupt = "Corrupt data or wrong password";
  if (data.size() < 5)
    throw std::runtime_error(corrupt);
  uint8_t filenameLength = data[0];
  if (filenameLength == 0)
    throw std::runtime_error(corrupt);
  size_t headerSize = 1 + filenameLength + 4;
  if (data.size() < headerSize)
    throw std::runtime_error(corrupt);
  std::string filename(data.begin() + 1, data.begin() + 1 + filenameLength);
  uint32_t secretLength;
  std::memcpy(&secretLength, &data[1 + filenameLength], 4);
  size_t totalSize = headerSize + secretLength;
  if (data.size() < totalSize)
    throw std::runtime_error(corrupt);

  std::filesystem::path outPath(outDir);
  if (!outPath.empty() && !std::filesystem::exists(outPath))
    std::filesystem::create_directories(outPath);
  std::filesystem::path fullPath = outPath / filename;

  std::ofstream outFile(fullPath, std::ios::binary);
  if (!outFile)
    throw std::runtime_error("Failed to create output file: " +
                             fullPath.string());
  outFile.write(reinterpret_cast<const char *>(data.data() + headerSize),
                secretLength);
  return fullPath.string();
}

} // namespace bpcs
} // namespace steg
