#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <cstdint>
#include <cmath>
#include <climits>

struct WavHeader {
    uint32_t riff;
    uint32_t fileSize;
    uint32_t wave;
    uint32_t fmt;
    uint32_t fmtSize;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    uint32_t dataHeader;
    uint32_t dataBytes;
};

std::vector<uint8_t> read_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Failed to open file: " + filename);
    std::vector<uint8_t> data(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(data.data()), data.size());
    return data;
}

void write_file(const std::string& filename, const std::vector<uint8_t>& data) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) throw std::runtime_error("Failed to open output file: " + filename);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

void vigenere_cipher(std::vector<uint8_t>& data, const std::string& key, bool encrypt) {
    int key_len = key.length();
    for (size_t i = 0; i < data.size(); ++i) {
        uint8_t k = key[i % key_len];
        data[i] = encrypt ? (data[i] + k) % 256 : (data[i] - k + 256) % 256;
    }
}

unsigned int generate_seed(const std::string& password) {
    unsigned int seed = 0;
    for (char c : password) seed += static_cast<unsigned int>(c);
    return seed;
}

std::vector<size_t> generate_indices(size_t data_size, unsigned int seed, bool randomize) {
    std::vector<size_t> indices(data_size);
    std::iota(indices.begin(), indices.end(), 0);
    if (randomize) {
        std::mt19937 rng(seed);
        std::shuffle(indices.begin(), indices.end(), rng);
    }
    return indices;
}

size_t find_data_chunk(const std::vector<uint8_t>& wav, size_t& data_size) {
    size_t pos = 12;
    while (pos < wav.size() - 8) {
        uint32_t chunk_id = *reinterpret_cast<const uint32_t*>(&wav[pos]);
        uint32_t chunk_size = *reinterpret_cast<const uint32_t*>(&wav[pos + 4]);
        if (chunk_id == 0x61746164) {
            data_size = chunk_size;
            return pos + 8;
        }
        pos += 8 + chunk_size;
    }
    throw std::runtime_error("Could not find data chunk");
}

void embed_data(const std::string& cover_file, const std::string& secret_file, const std::string& output_file, const std::string& password, bool encrypt, bool randomize) {
    auto wav = read_file(cover_file);
    size_t data_size;
    size_t data_start = find_data_chunk(wav, data_size);
    
    // Save original data for PSNR calculation
    std::vector<uint8_t> original_data(wav.begin() + data_start, wav.begin() + data_start + data_size);
    
    auto secret = read_file(secret_file);
    std::string filename = secret_file.substr(secret_file.find_last_of("/\\") + 1);
    
    std::vector<uint8_t> header;
    uint32_t filename_len = filename.size();
    header.insert(header.end(), reinterpret_cast<uint8_t*>(&filename_len), reinterpret_cast<uint8_t*>(&filename_len) + 4);
    header.insert(header.end(), filename.begin(), filename.end());
    uint64_t secret_size = secret.size();
    header.insert(header.end(), reinterpret_cast<uint8_t*>(&secret_size), reinterpret_cast<uint8_t*>(&secret_size) + 8);
    header.insert(header.end(), secret.begin(), secret.end());
    
    if (encrypt) vigenere_cipher(header, password, true);
    
    std::vector<uint8_t> full_data;
    uint64_t header_size = header.size();
    full_data.insert(full_data.end(), reinterpret_cast<uint8_t*>(&header_size), reinterpret_cast<uint8_t*>(&header_size) + 8);
    full_data.insert(full_data.end(), header.begin(), header.end());
    
    size_t required_bits = full_data.size() * 8;
    if (required_bits > data_size) {
        throw std::runtime_error("Insufficient capacity: " + std::to_string(required_bits) + 
                                 " bits required, " + std::to_string(data_size) + " available");
    }
    
    unsigned int seed = generate_seed(password);
    auto indices = generate_indices(data_size, seed, randomize);
    
    for (size_t i = 0; i < full_data.size(); ++i) {
        uint8_t byte = full_data[i];
        for (int bit = 7; bit >= 0; --bit) {
            size_t idx = i * 8 + (7 - bit);
            if (idx >= indices.size()) throw std::runtime_error("Index out of bounds");
            size_t pos = data_start + indices[idx];
            wav[pos] = (wav[pos] & 0xFE) | ((byte >> bit) & 1);
        }
    }
    
    // Calculate PSNR
    double P0 = 0.0, P1 = 0.0;
    for (size_t i = 0; i < data_size; ++i) {
        P0 += original_data[i] * original_data[i];
        P1 += wav[data_start + i] * wav[data_start + i];
    }
    
    double numerator = P1 * P1;
    double denominator = (P1 - P0) * (P1 - P0);
    
    std::cout << "PSNR: ";
    if (denominator == 0) {
        std::cout << "Infinite dB (no changes detected)" << std::endl;
    } else {
        double psnr = 10.0 * log10(numerator / denominator);
        std::cout << psnr << " dB" << std::endl;
        if (psnr < 30.0) {
            std::cerr << "WARNING: PSNR below 30 dB - Audio quality may be significantly degraded" << std::endl;
        }
    }
    
    write_file(output_file, wav);
}

void extract_data(const std::string& stego_file, const std::string& password, bool encrypt, bool randomize) {
    
    auto wav = read_file(stego_file);
    size_t data_size;
    size_t data_start = find_data_chunk(wav, data_size);
    
    unsigned int seed = generate_seed(password);
    auto indices = generate_indices(data_size, seed, randomize);
    
    std::vector<uint8_t> header_size_bytes(8);
    for (size_t i = 0; i < 8; ++i) {
        uint8_t byte = 0;
        for (int bit = 7; bit >= 0; --bit) {
            size_t idx = i * 8 + (7 - bit);
            if (idx >= indices.size()) throw std::runtime_error("Index out of bounds");
            size_t pos = data_start + indices[idx];
            byte |= (wav[pos] & 1) << bit;
        }
        header_size_bytes[i] = byte;
    }
    
    uint64_t header_size = *reinterpret_cast<uint64_t*>(header_size_bytes.data());
    if (header_size > data_size) {
        throw std::runtime_error("Invalid header size");
    }
    
    std::vector<uint8_t> header(header_size);
    for (size_t i = 0; i < header.size(); ++i) {
        uint8_t byte = 0;
        for (int bit = 7; bit >= 0; --bit) {
            size_t idx = 8 * 8 + i * 8 + (7 - bit);
            if (idx >= indices.size()) throw std::runtime_error("Index out of bounds");
            size_t pos = data_start + indices[idx];
            byte |= (wav[pos] & 1) << bit;
        }
        header[i] = byte;
    }
    
    if (encrypt) vigenere_cipher(header, password, false);
    
    uint32_t filename_len = *reinterpret_cast<uint32_t*>(header.data());
    std::string filename(reinterpret_cast<char*>(header.data() + 4), filename_len);
    uint64_t secret_size = *reinterpret_cast<uint64_t*>(header.data() + 4 + filename_len);
    std::vector<uint8_t> secret(header.begin() + 4 + filename_len + 8, 
                                header.begin() + 4 + filename_len + 8 + secret_size);
    
    write_file(filename, secret);
}

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            std::cerr << "Usage:\n"
                      << "  " << argv[0] << " embed <cover.wav> <secret> <output> [password] [-e] [-r]\n"
                      << "  " << argv[0] << " extract <stego.wav> [password] [-e] [-r]\n"
                      << "\nOptions:\n"
                      << "  -e   Vigenere-encrypt the payload with <password>.\n"
                      << "       NOTE: this is a byte-shift obfuscation, NOT strong encryption.\n"
                      << "  -r   Randomize sample positions using <password> as the seed.\n"
                      << "  Extract must use the SAME password and -e/-r flags as embed.\n";
            return 1;
        }
        
        std::string mode(argv[1]);
        if (mode == "embed") {
            if (argc < 5) {
                std::cerr << "Usage: " << argv[0] << " embed <cover.wav> <secret> <output> [password] [-e] [-r]\n";
                return 1;
            }
            
            std::string cover = argv[2];
            std::string secret = argv[3];
            std::string output = argv[4];
            std::string password;
            bool encrypt = false;
            bool randomize = false;
            
            for (int i = 5; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "-e") encrypt = true;
                else if (arg == "-r") randomize = true;
                else password = arg;
            }
            
            embed_data(cover, secret, output, password, encrypt, randomize);
            std::cout << "Embedding successful!" << std::endl;
            
        } else if (mode == "extract") {
            if (argc < 3) {
                std::cerr << "Usage: " << argv[0] << " extract <stego.wav> [password] [-e] [-r]\n";
                return 1;
            }
            
            std::string stego = argv[2];
            std::string password;
            bool encrypt = false;
            bool randomize = false;
            
            for (int i = 3; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "-e") encrypt = true;
                else if (arg == "-r") randomize = true;
                else password = arg;
            }
            
            extract_data(stego, password, encrypt, randomize);
            std::cout << "Extraction successful!" << std::endl;
            
        } else {
            std::cerr << "Invalid mode. Use 'embed' or 'extract'" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}