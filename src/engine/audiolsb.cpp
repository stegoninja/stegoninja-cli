#include "engine/audiolsb.h"
#include "vigenere.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

namespace steg {
namespace audiolsb {

namespace {

std::vector<uint8_t> readFile(const std::string &filename) {
  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file)
    throw std::runtime_error("Failed to open file: " + filename);
  std::vector<uint8_t> data(file.tellg());
  file.seekg(0, std::ios::beg);
  if (!data.empty())
    file.read(reinterpret_cast<char *>(data.data()), data.size());
  return data;
}

void writeFile(const std::string &filename, const std::vector<uint8_t> &data) {
  std::ofstream file(filename, std::ios::binary);
  if (!file)
    throw std::runtime_error("Failed to open output file: " + filename);
  file.write(reinterpret_cast<const char *>(data.data()), data.size());
}

unsigned int generateSeed(const std::string &password) {
  unsigned int seed = 0;
  for (char c : password)
    seed += static_cast<unsigned int>(c);
  return seed;
}

std::vector<size_t> generateIndices(size_t dataSize, unsigned int seed,
                                    bool randomize) {
  std::vector<size_t> indices(dataSize);
  std::iota(indices.begin(), indices.end(), 0);
  if (randomize) {
    std::mt19937 rng(seed);
    std::shuffle(indices.begin(), indices.end(), rng);
  }
  return indices;
}

// Locate the WAV data chunk, returning its byte offset and setting dataSize.
size_t findDataChunk(const std::vector<uint8_t> &wav, size_t &dataSize) {
  if (wav.size() < 12)
    throw std::runtime_error("Not a WAV file (too short)");
  size_t pos = 12;
  while (pos + 8 <= wav.size()) {
    uint32_t chunkId = *reinterpret_cast<const uint32_t *>(&wav[pos]);
    uint32_t chunkSize = *reinterpret_cast<const uint32_t *>(&wav[pos + 4]);
    if (chunkId == 0x61746164) { // "data"
      dataSize = chunkSize;
      size_t dataStart = pos + 8;
      if (dataSize > wav.size() - dataStart)
        throw std::runtime_error("Corrupt WAV: data chunk larger than file");
      return dataStart;
    }
    pos += 8 + chunkSize;
  }
  throw std::runtime_error("Could not find WAV data chunk");
}

} // namespace

double embed(const std::string &coverPath, const std::string &secretPath,
             const std::string &outPath, const std::string &password,
             bool encrypt, bool randomize) {
  auto wav = readFile(coverPath);
  size_t dataSize;
  size_t dataStart = findDataChunk(wav, dataSize);

  std::vector<uint8_t> originalData(wav.begin() + dataStart,
                                    wav.begin() + dataStart + dataSize);

  auto secret = readFile(secretPath);
  std::string filename =
      std::filesystem::path(secretPath).filename().string();

  // Header: [u32 filename_len][filename][u64 secret_size][secret].
  std::vector<uint8_t> header;
  uint32_t filenameLen = static_cast<uint32_t>(filename.size());
  header.insert(header.end(), reinterpret_cast<uint8_t *>(&filenameLen),
                reinterpret_cast<uint8_t *>(&filenameLen) + 4);
  header.insert(header.end(), filename.begin(), filename.end());
  uint64_t secretSize = secret.size();
  header.insert(header.end(), reinterpret_cast<uint8_t *>(&secretSize),
                reinterpret_cast<uint8_t *>(&secretSize) + 8);
  header.insert(header.end(), secret.begin(), secret.end());

  if (encrypt)
    header = Vigenere::vigenereEncrypt(header, password);

  // Full stream = [u64 header_size][header].
  std::vector<uint8_t> fullData;
  uint64_t headerSize = header.size();
  fullData.insert(fullData.end(), reinterpret_cast<uint8_t *>(&headerSize),
                  reinterpret_cast<uint8_t *>(&headerSize) + 8);
  fullData.insert(fullData.end(), header.begin(), header.end());

  size_t requiredBits = fullData.size() * 8;
  if (requiredBits > dataSize)
    throw std::runtime_error("Insufficient capacity: " +
                             std::to_string(requiredBits) + " bits required, " +
                             std::to_string(dataSize) + " available");

  auto indices = generateIndices(dataSize, generateSeed(password), randomize);
  for (size_t i = 0; i < fullData.size(); ++i) {
    uint8_t byte = fullData[i];
    for (int bit = 7; bit >= 0; --bit) {
      size_t idx = i * 8 + (7 - bit);
      size_t pos = dataStart + indices[idx];
      wav[pos] = (wav[pos] & 0xFE) | ((byte >> bit) & 1);
    }
  }

  double P0 = 0.0, P1 = 0.0;
  for (size_t i = 0; i < dataSize; ++i) {
    P0 += static_cast<double>(originalData[i]) * originalData[i];
    P1 += static_cast<double>(wav[dataStart + i]) * wav[dataStart + i];
  }
  double denominator = (P1 - P0) * (P1 - P0);

  writeFile(outPath, wav);
  return denominator == 0 ? INFINITY : 10.0 * std::log10((P1 * P1) / denominator);
}

std::string extract(const std::string &stegoPath, const std::string &outDir,
                    const std::string &password, bool encrypt, bool randomize) {
  auto wav = readFile(stegoPath);
  size_t dataSize;
  size_t dataStart = findDataChunk(wav, dataSize);

  auto indices = generateIndices(dataSize, generateSeed(password), randomize);

  auto readBits = [&](size_t bitOffset, size_t byteCount) {
    std::vector<uint8_t> out(byteCount, 0);
    for (size_t i = 0; i < byteCount; ++i) {
      uint8_t byte = 0;
      for (int bit = 7; bit >= 0; --bit) {
        size_t idx = bitOffset + i * 8 + (7 - bit);
        if (idx >= indices.size())
          throw std::runtime_error("Index out of bounds (corrupt stego)");
        byte |= (wav[dataStart + indices[idx]] & 1) << bit;
      }
      out[i] = byte;
    }
    return out;
  };

  auto headerSizeBytes = readBits(0, 8);
  uint64_t headerSize = *reinterpret_cast<uint64_t *>(headerSizeBytes.data());
  if (headerSize > dataSize)
    throw std::runtime_error("Invalid header size (wrong password/flags?)");

  auto header = readBits(8 * 8, headerSize);
  if (encrypt)
    header = Vigenere::vigenereDecrypt(header, password);

  // Validate every field before indexing so a wrong password/flags (or corrupt
  // stego) fails cleanly instead of reading out of bounds.
  const char *corrupt = "Corrupt data, or wrong password / encrypt / randomize";
  if (header.size() < 4)
    throw std::runtime_error(corrupt);
  uint32_t filenameLen = *reinterpret_cast<uint32_t *>(header.data());
  if (static_cast<uint64_t>(4) + filenameLen + 8 > header.size())
    throw std::runtime_error(corrupt);
  std::string filename(reinterpret_cast<char *>(header.data() + 4), filenameLen);
  uint64_t secretSize =
      *reinterpret_cast<uint64_t *>(header.data() + 4 + filenameLen);
  if (static_cast<uint64_t>(4) + filenameLen + 8 + secretSize > header.size())
    throw std::runtime_error(corrupt);

  std::vector<uint8_t> secret(header.begin() + 4 + filenameLen + 8,
                              header.begin() + 4 + filenameLen + 8 + secretSize);

  std::filesystem::path outPath(outDir);
  if (!outPath.empty() && !std::filesystem::exists(outPath))
    std::filesystem::create_directories(outPath);
  std::filesystem::path fullPath = outPath / filename;
  writeFile(fullPath.string(), secret);
  return fullPath.string();
}

} // namespace audiolsb
} // namespace steg
