#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <random>
#include <filesystem>
#include <cmath>

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

void writeBMP(const std::string& filename, const std::vector<RGB>& pixels, int width, int height) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) throw std::runtime_error("Failed to create BMP file");

    int rowSize = (width * 3 + 3) & ~3;
    int dataSize = rowSize * height;
    int fileSize = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + dataSize;

    BMPFileHeader fileHeader;
    fileHeader.fileSize = fileSize;
    fileHeader.offsetData = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader);

    BMPInfoHeader infoHeader;
    infoHeader.width = width;
    infoHeader.height = -height;

    file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

    std::vector<uint8_t> row(rowSize);
    
    for (int y = 0; y < height; ++y) {
        const RGB* srcRow = pixels.data() + y * width;
        
        for (int x = 0; x < width; ++x) {
            row[x * 3 + 0] = srcRow[x].b;
            row[x * 3 + 1] = srcRow[x].g;
            row[x * 3 + 2] = srcRow[x].r;
        }
        
        std::fill(row.begin() + width * 3, row.end(), 0);
        file.write(reinterpret_cast<const char*>(row.data()), rowSize);
    }
}

std::vector<uint8_t> readSecretFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw std::runtime_error("Failed to read secret file");
    }
    
    return buffer;
}

std::vector<uint8_t> vigenereEncrypt(const std::vector<uint8_t>& data, const std::string& key) {
    std::vector<uint8_t> result(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        uint8_t keyByte = key.empty() ? 0 : key[i % key.size()];
        result[i] = (data[i] + keyByte) % 256;
    }
    return result;
}

int main(int argc, char* argv[]) {
    if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        std::cout << "Usage: " << argv[0] << " <cover.bmp> <secret_file> <output.bmp>\n"
                  << "Embed <secret_file> into a 24-bit BMP cover using BPCS.\n"
                  << "Prompts for a password on stdin (blank = no encryption/randomization).\n";
        return 0;
    }
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <cover.bmp> <secret_file> <output.bmp>\n";
        return 1;
    }

    std::string coverFile = argv[1];
    std::string secretFile = argv[2];
    std::string outputFile = argv[3];

    // Get secret filename
    std::string secretFilename = std::filesystem::path(secretFile).filename().string();

    int width, height;
    std::vector<RGB> originalPixels = readBMP(coverFile, width, height);
    std::vector<RGB> pixels = originalPixels; // Working copy for modification

    std::vector<uint8_t> secretData = readSecretFile(secretFile);

    // Prompt for password
    std::string password;
    std::cout << "Enter password (leave blank for no encryption/randomization): ";
    std::getline(std::cin, password);
    bool useEncryption = !password.empty();
    bool useRandomization = !password.empty();

    // Prepare data to embed
    std::vector<uint8_t> dataToEmbed;

    // Add filename
    if (secretFilename.size() > 255) {
        throw std::runtime_error("Filename exceeds maximum length of 255 characters");
    }
    uint8_t filenameLength = static_cast<uint8_t>(secretFilename.size());
    dataToEmbed.push_back(filenameLength); // Store as single byte
    dataToEmbed.insert(dataToEmbed.end(), secretFilename.begin(), secretFilename.end());

    // Add secret data length and data
    uint32_t secretLength = secretData.size();
    dataToEmbed.insert(dataToEmbed.end(), reinterpret_cast<uint8_t*>(&secretLength), 
                       reinterpret_cast<uint8_t*>(&secretLength) + 4);
    dataToEmbed.insert(dataToEmbed.end(), secretData.begin(), secretData.end());

    // Encrypt if needed
    if (useEncryption) {
        dataToEmbed = vigenereEncrypt(dataToEmbed, password);
    }

    // Convert to bitstream
    std::vector<bool> secretBitstream;
    for (uint8_t byte : dataToEmbed) {
        for (int i = 7; i >= 0; --i) {
            secretBitstream.push_back((byte >> i) & 1);
        }
    }

    // Collect eligible blocks
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

    // Check capacity
    size_t availableBits = eligibleBlocks.size() * 64;
    size_t requiredBits = secretBitstream.size();
    
    if (requiredBits > availableBits) {
        throw std::runtime_error("Secret data too large. Maximum capacity: " + 
                                std::to_string(availableBits / 8) + " bytes");
    }

    // Shuffle if needed
    if (useRandomization) {
        unsigned int seed = 0;
        for (char c : password) {
            seed += static_cast<unsigned int>(c);
        }
        std::shuffle(eligibleBlocks.begin(), eligibleBlocks.end(), std::default_random_engine(seed));
    }

    // Embed data
    size_t bitIndex = 0;
    for (const auto& pos : eligibleBlocks) {
        if (bitIndex >= secretBitstream.size()) break;

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

        // Replace bits with secret data
        for (int i = 0; i < 8 && bitIndex < secretBitstream.size(); ++i) {
            for (int j = 0; j < 8 && bitIndex < secretBitstream.size(); ++j) {
                block[i][j] = secretBitstream[bitIndex++];
            }
        }

        // Update pixels
        for (int i = 0; i < 8; ++i) {
            for (int j = 0; j < 8; ++j) {
                int pixelIndex = (pos.y + i) * width + (pos.x + j);
                uint8_t& value = (pos.channel == 0) ? pixels[pixelIndex].r :
                                 (pos.channel == 1) ? pixels[pixelIndex].g : pixels[pixelIndex].b;
                value &= ~(1 << (7 - pos.bitPlane));
                value |= (block[i][j] << (7 - pos.bitPlane));
            }
        }
    }

    // Calculate PSNR
    double sum = 0.0;
    for (size_t i = 0; i < pixels.size(); ++i) {
        sum += std::pow(originalPixels[i].r - pixels[i].r, 2);
        sum += std::pow(originalPixels[i].g - pixels[i].g, 2);
        sum += std::pow(originalPixels[i].b - pixels[i].b, 2);
    }
    
    double mse = sum / (3 * width * height);
    double rms = std::sqrt(mse);
    double psnr = 0.0;
    
    if (mse == 0) {
        std::cout << "PSNR: Infinite dB (no changes made)" << std::endl;
    } else {
        psnr = 20 * std::log10(256.0 / rms);
        std::cout << "PSNR: " << psnr << " dB" << std::endl;
    }

    writeBMP(outputFile, pixels, width, height);
    std::cout << "Data embedded successfully: " << outputFile << std::endl;

    return 0;
}