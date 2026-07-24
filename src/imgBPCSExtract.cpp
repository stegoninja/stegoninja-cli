#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <random>
#include <filesystem>

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
    int channel;
    int bitPlane;
    int x;
    int y;
};

std::vector<RGB> readBMP(const std::string& filename, int& width, int& height) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) throw std::runtime_error("Failed to open BMP file");

    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;
    
    file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
    file.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));

    if (fileHeader.fileType != 0x4D42 || infoHeader.headerSize != 40 || 
        infoHeader.bitCount != 24 || infoHeader.compression != 0) {
        throw std::runtime_error("Unsupported BMP format");
    }

    width = infoHeader.width;
    height = std::abs(infoHeader.height);
    int rowSize = (width * 3 + 3) & ~3;
    int dataSize = rowSize * height;

    std::vector<uint8_t> data(dataSize);
    file.seekg(fileHeader.offsetData);
    file.read(reinterpret_cast<char*>(data.data()), dataSize);

    std::vector<RGB> pixels(width * height);
    bool isBottomUp = infoHeader.height > 0;

    for (int y = 0; y < height; ++y) {
        int srcY = isBottomUp ? height - 1 - y : y;
        const uint8_t* row = data.data() + srcY * rowSize;
        
        for (int x = 0; x < width; ++x) {
            pixels[y * width + x].r = row[x * 3 + 2];
            pixels[y * width + x].g = row[x * 3 + 1];
            pixels[y * width + x].b = row[x * 3 + 0];
        }
    }

    return pixels;
}

std::vector<uint8_t> vigenereDecrypt(const std::vector<uint8_t>& data, const std::string& key) {
    std::vector<uint8_t> result(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        uint8_t keyByte = key.empty() ? 0 : key[i % key.size()];
        result[i] = (data[i] - keyByte + 256) % 256;
    }
    return result;
}

int main(int argc, char* argv[]) {
    if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        std::cout << "Usage: " << argv[0] << " <stego.bmp> <output_directory>\n"
                  << "Extract a BPCS-hidden file from a 24-bit BMP into <output_directory>.\n"
                  << "Prompts for the password used at embed time (blank = none).\n";
        return 0;
    }
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <stego.bmp> <output_directory>\n";
        return 1;
    }

    std::string stegoFile = argv[1];
    std::string outputDir = argv[2];
    std::filesystem::path outputPath(outputDir);

    int width, height;
    std::vector<RGB> pixels = readBMP(stegoFile, width, height);

    std::string password;
    std::cout << "Enter password (leave blank if none): ";
    std::getline(std::cin, password);
    bool useDecryption = !password.empty();

    // Reconstruct eligible blocks
    std::vector<BlockPosition> eligibleBlocks;
    const int threshold = 34;

    for (int channel = 0; channel < 3; ++channel) {
        for (int bitPlane = 7; bitPlane >= 0; --bitPlane) {
            for (int y = 0; y < height; y += 8) {
                for (int x = 0; x < width; x += 8) {
                    std::vector<std::vector<bool>> block(8, std::vector<bool>(8));
                    
                    // Extract block bits
                    for (int i = 0; i < 8; ++i) {
                        for (int j = 0; j < 8; ++j) {
                            int pixelIndex = (y + i) * width + (x + j);
                            uint8_t value = (channel == 0) ? pixels[pixelIndex].r :
                                            (channel == 1) ? pixels[pixelIndex].g : pixels[pixelIndex].b;
                            block[i][j] = (value >> (7 - bitPlane)) & 1;
                        }
                    }

                    // Calculate complexity
                    int complexity = 0;
                    for (int i = 0; i < 8; ++i) {
                        for (int j = 0; j < 8; ++j) {
                            if (j < 7 && block[i][j] != block[i][j + 1]) complexity++;
                            if (i < 7 && block[i][j] != block[i + 1][j]) complexity++;
                        }
                    }

                    if (complexity >= threshold) {
                        eligibleBlocks.push_back({channel, bitPlane, x, y});
                    }
                }
            }
        }
    }

    // Shuffle if needed
    if (useDecryption) {
        unsigned int seed = 0;
        for (char c : password) {
            seed += static_cast<unsigned int>(c);
        }
        std::shuffle(eligibleBlocks.begin(), eligibleBlocks.end(), std::default_random_engine(seed));
    }

    // Extract bitstream
    std::vector<bool> extractedBitstream;
    for (const auto& pos : eligibleBlocks) {
        std::vector<std::vector<bool>> block(8, std::vector<bool>(8));
        
        // Extract current block bits
        for (int i = 0; i < 8; ++i) {
            for (int j = 0; j < 8; ++j) {
                int pixelIndex = (pos.y + i) * width + (pos.x + j);
                uint8_t value = (pos.channel == 0) ? pixels[pixelIndex].r :
                                (pos.channel == 1) ? pixels[pixelIndex].g : pixels[pixelIndex].b;
                block[i][j] = (value >> (7 - pos.bitPlane)) & 1;
            }
        }

        // Add all 64 bits from the block
        for (int i = 0; i < 8; ++i) {
            for (int j = 0; j < 8; ++j) {
                extractedBitstream.push_back(block[i][j]);
            }
        }
    }

    // Convert to bytes
    std::vector<uint8_t> encryptedData;
    for (size_t i = 0; i < extractedBitstream.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8 && (i + j) < extractedBitstream.size(); ++j) {
            byte |= (extractedBitstream[i + j] << (7 - j));
        }
        encryptedData.push_back(byte);
    }

    // Decrypt if needed
    if (useDecryption) {
        encryptedData = vigenereDecrypt(encryptedData, password);
    }

    // Parse decrypted data
    if (encryptedData.size() < 8) {
        throw std::runtime_error("Invalid data: too short");
    }

    // Read filename length
    uint8_t filenameLength = encryptedData[0]; // Read single byte
    if (filenameLength == 0) {
        throw std::runtime_error("Invalid filename length");
    }

    // Verify total data length
    size_t headerSize = 1 + filenameLength + 4; // 1 byte length + filename + 4 bytes secret length
    if (encryptedData.size() < headerSize) {
        throw std::runtime_error("Data truncated");
    }

    // Extract filename
    std::string filename(encryptedData.begin() + 1, 
                        encryptedData.begin() + 1 + filenameLength);

    // Read secret data length
    uint32_t secretLength = *reinterpret_cast<const uint32_t*>(&encryptedData[1 + filenameLength]);

    // Verify secret data presence
    size_t totalSize = headerSize + secretLength;
    if (encryptedData.size() < totalSize) {
        throw std::runtime_error("Data truncated");
    }

    // Extract secret data
    std::vector<uint8_t> secretData(
        encryptedData.begin() + headerSize,
        encryptedData.begin() + totalSize
    );

    // Create output directory if needed
    if (!std::filesystem::exists(outputPath)) {
        std::filesystem::create_directories(outputPath);
    }

    std::filesystem::path fullOutputPath = outputPath / filename;
    std::ofstream outFile(fullOutputPath, std::ios::binary);
    if (!outFile) {
        throw std::runtime_error("Failed to create output file");
    }

    outFile.write(reinterpret_cast<const char*>(secretData.data()), secretData.size());
    std::cout << "Data extracted successfully to: " << fullOutputPath << std::endl;

    return 0;
}