#include <bitset>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <opencv2/opencv.hpp>
#include <random>
#include <string>
#include <vector>

#include "vigenere.h"  // shared Vigenere byte-cipher (single implementation)

using namespace cv;
using namespace std;

std::string original_path;
std::string stego_path;

// Options for the two operations. When interactive is true the tools prompt for
// any value on stdin exactly as before; when false the values here are used
// verbatim (non-interactive / scriptable mode).
struct VidOpts {
  bool interactive = true;
  int frameMode = 1;   // 1 = sequential, 2 = random
  int pixelMode = 1;   // 1 = sequential, 2 = random
  int encryptFlag = 0; // 0/1
  int seed = 0;        // 0..31
  bool saveToFile = false;
  std::string key;
  std::string inPath;   // cover (embed) / stego (extract)
  std::string outPath;  // stego .avi (embed) / recovered message file (extract)
  std::string message;  // secret text (embed)
};

// ====================== Common Functions ======================
vector<bool> messageToBits(const string &message) {
  vector<bool> bits;
  for (char c : message)
    for (int i = 7; i >= 0; --i)
      bits.push_back((c >> i) & 1);
  return bits;
}

// Vigenere byte-cipher now lives in the shared Vigenere:: namespace
// (src/vigenere.cpp); call Vigenere::vigenere_encrypt / vigenere_decrypt.

void embedMetadata(cv::Mat &frame, uint8_t metadata) {
  if (frame.empty()) {
    std::cerr << "Frame is empty!\n";
    return;
  }

  if (frame.cols < 8) {
    std::cerr << "Frame not wide enough to embed metadata!\n";
    return;
  }

  for (int p = 0; p < 8; p++) {
    cv::Vec3b &pixel = frame.at<cv::Vec3b>(0, p);

    // Extract the relevant bit from metadata (little-endian)
    uint8_t bit = (metadata >> p) & 1;

    // Clear LSB of the blue channel, then set it
    pixel[0] = (pixel[0] & 0xFE) | bit;
  }

  std::cout << "[*] Embedded metadata: " << (int)metadata << "\n";
}

string bitsToMessage(const vector<bool> &bits) {
  string message;
  for (size_t i = 0; i < bits.size(); i += 8) {
    char c = 0;
    for (int j = 0; j < 8; ++j)
      if (i + j < bits.size()) // Safety check
        c |= bits[i + j] << (7 - j);
    message += c;
  }
  return message;
}

vector<bool> getLengthBits(uint32_t lengthInBits) {
  vector<bool> lengthBits;
  for (int i = 31; i >= 0; --i)
    lengthBits.push_back((lengthInBits >> i) & 1);
  return lengthBits;
}

// ====================== Embed Functions ======================
void embedBitsInFrame(Mat &frame, const vector<bool> &bits, int &bitIndex,
                      bool randomPixels, mt19937 &rng) {
  int rows = frame.rows;
  int cols = frame.cols;
  int channels = frame.channels();

  vector<pair<int, int>> pixelIndices;
  for (int y = 0; y < rows; ++y)
    for (int x = 0; x < cols; ++x)
      pixelIndices.push_back({y, x});

  if (randomPixels)
    shuffle(pixelIndices.begin(), pixelIndices.end(), rng);

  for (auto [y, x] : pixelIndices) {
    Vec3b &pixel = frame.at<Vec3b>(y, x);
    for (int c = 0; c < channels; ++c) {
      if (bitIndex >= bits.size())
        return;
      pixel[c] = (pixel[c] & ~1) | bits[bitIndex++];
    }
    if (bitIndex >= bits.size())
      return;
  }
}

void embedMessage(const VidOpts &o = VidOpts()) {
  string inputVideoPath, outputVideoPath, message, key;
  int frameMode, pixelMode, encryptFlag;

  if (o.interactive) {
    cout << "=== EMBED MODE ===\n";
    cout << "Frame Mode:\n1. Sequential\n2. Random\nChoose: ";
    cin >> frameMode;
    cout << "Pixel Mode:\n1. Sequential\n2. Random\nChoose: ";
    cin >> pixelMode;
    cout << "Use Vigenere encryption? [obfuscation, NOT strong crypto] (1 = Yes / 0 = No): ";
    cin >> encryptFlag;

    if (encryptFlag) {
      cout << "Enter key: ";
      cin >> key;
    }

    cout << "Input video path: ";
    cin >> inputVideoPath;
    cout << "Output video path (should end with .avi): ";
    cin >> outputVideoPath;

    cout << "Enter message: ";
    cin.ignore();
    getline(cin, message);
  } else {
    frameMode = o.frameMode;
    pixelMode = o.pixelMode;
    encryptFlag = o.encryptFlag;
    key = o.key;
    inputVideoPath = o.inPath;
    outputVideoPath = o.outPath;
    message = o.message;
  }
  original_path = inputVideoPath;
  stego_path = outputVideoPath;

  if (encryptFlag) {
    message = Vigenere::vigenere_encrypt(message, key);
  }
  message += '\0'; // Null terminator to mark end
  vector<bool> messageBits = messageToBits(message);
  uint32_t messageBitLength = messageBits.size();
  vector<bool> lengthBits = getLengthBits(messageBitLength);

  VideoCapture cap(inputVideoPath);
  if (!cap.isOpened()) {
    cerr << "Failed to open input video.\n";
    return;
  }

  int frameWidth = static_cast<int>(cap.get(CAP_PROP_FRAME_WIDTH));
  int frameHeight = static_cast<int>(cap.get(CAP_PROP_FRAME_HEIGHT));
  double fps = cap.get(CAP_PROP_FPS);
  int totalFrames = static_cast<int>(cap.get(CAP_PROP_FRAME_COUNT));

  cout << "Video Info: " << frameWidth << "x" << frameHeight << ", " << fps
       << " FPS, " << totalFrames << " frames\n";

  VideoWriter writer;
  int codec = VideoWriter::fourcc('F', 'F', 'V', '1');
  writer.open(outputVideoPath, codec, fps, Size(frameWidth, frameHeight), true);

  if (!writer.isOpened()) {
    cerr << "Failed to open output video.\n";
    return;
  }
  int seed;
  if (o.interactive) {
    cout << "Enter seed : ";
    cin >> seed;
  } else {
    seed = o.seed;
  }

  uint8_t metadata =
      (frameMode == 2) << 7 | (pixelMode == 2) << 6 | (encryptFlag) << 5;
  metadata |= (seed & 0x1F); // 5 bits for seed (0-31)
  cout << "frame mode : " << frameMode << " pixel mode : " << pixelMode
       << " metadata : " << static_cast<int>(metadata) << std::endl;
  int bitIndex = 0;
  mt19937 rng(seed);

  // Generate frame order if random
  vector<int> frameIndices(totalFrames);
  iota(frameIndices.begin(), frameIndices.end(), 0);
  if (frameMode == 2)
    shuffle(frameIndices.begin() + 1, frameIndices.end(),
            rng); // Skip first frame for metadata

  for (int i = 0; i < totalFrames; ++i) {
    int frameNumber = (frameMode == 2) ? frameIndices[i] : i;
    cap.set(CAP_PROP_POS_FRAMES, frameNumber);

    Mat frame;
    cap.read(frame);
    if (frame.empty())
      break;

    if (frameNumber == 0) {

      embedMetadata(frame, metadata);

      for (int p = 0; p < 32; p++) {
        int pixelIdx = p + 8;
        int x = pixelIdx % frame.cols;
        int y = pixelIdx / frame.cols;

        if (y >= frame.rows)
          break;

        frame.at<Vec3b>(y, x)[0] =
            (frame.at<Vec3b>(y, x)[0] & ~1) | lengthBits[p];
      }
      cout << "[*] Metadata + length embedded in first frame.\n";
    } else {
      embedBitsInFrame(frame, messageBits, bitIndex, (pixelMode == 2), rng);
    }

    writer.write(frame);

    if (bitIndex >= messageBits.size()) {
      cout << "[*] Message completely embedded!\n";
      // Fill remaining frames without changes
      for (int j = i + 1; j < totalFrames; ++j) {
        cap.set(CAP_PROP_POS_FRAMES, (frameMode == 2) ? frameIndices[j] : j);
        Mat restFrame;
        if (!cap.read(restFrame) || restFrame.empty())
          break;
        writer.write(restFrame);
      }
      break;
    }
  }

  if (bitIndex < messageBits.size()) {
    cerr << "[!] Not enough space to embed the entire message!\n";
  } else {
    cout << "[*] Message embedded successfully!\n";
  }

  cap.release();
  writer.release();
}

// ====================== Extract Functions ======================
uint8_t extractMetadata(const Mat &frame) {
  uint8_t metadata = 0;

  // Check if the frame is wide enough
  if (frame.cols < 8) {
    throw std::runtime_error("Frame too small to contain metadata!");
  }

  for (int p = 0; p < 8; p++) {
    // Extract LSB from the blue channel of the first row
    uint8_t lsb = frame.at<Vec3b>(0, p)[0] & 1;

    // Shift it to its position and OR it into metadata
    metadata |= (lsb << p); // little-endian
    // metadata |= (lsb << (7 - p)); // big-endian (if you prefer)
  }

  return metadata;
}

uint32_t extractLength(const Mat &frame) {
  uint32_t lengthBits = 0;
  for (int p = 0; p < 32; p++) {
    int pixelIdx = p + 8;
    int x = pixelIdx % frame.cols;
    int y = pixelIdx / frame.cols;
    if (y >= frame.rows)
      break;
    lengthBits |= (frame.at<Vec3b>(y, x)[0] & 1) << (31 - p);
  }
  return lengthBits;
}

void extractMessageBits(vector<bool> &bits, VideoCapture &cap, int totalBits,
                        bool pixelMode, bool frameMode, int totalFrames,
                        uint32_t seed) {

  cout << "Seed : " << seed << std::endl;
  mt19937 rng(seed);

  vector<int> frameIndices(totalFrames);
  iota(frameIndices.begin(), frameIndices.end(), 0);
  if (frameMode)
    shuffle(frameIndices.begin() + 1, frameIndices.end(), rng);

  // Start from frame 1, since frame 0 contains metadata.
  // Embed writes frames in sequential output order (the frame shuffle above only
  // chooses which source frame supplies each slot and, crucially, advances the
  // shared RNG so per-frame pixel shuffles stay in sync). The message therefore
  // lands in stego frames 1,2,3,... in order, so read them sequentially.
  for (int i = 1; i < totalFrames && totalBits > 0; ++i) {
    int frameNumber = i;
    cap.set(CAP_PROP_POS_FRAMES, frameNumber);

    Mat frame;
    if (!cap.read(frame) || frame.empty())
      break;

    int rows = frame.rows;
    int cols = frame.cols;
    vector<pair<int, int>> pixelIndices;

    for (int y = 0; y < rows; ++y)
      for (int x = 0; x < cols; ++x)
        pixelIndices.push_back({y, x});

    if (pixelMode)
      shuffle(pixelIndices.begin(), pixelIndices.end(), rng);

    for (auto [y, x] : pixelIndices) {
      Vec3b pixel = frame.at<Vec3b>(y, x);
      for (int c = 0; c < 3; ++c) {
        if (totalBits <= 0)
          return;
        bits.push_back(pixel[c] & 1);
        totalBits--;
      }
      if (totalBits <= 0)
        return;
    }
  }
}

void extractRandomBits(const cv::Mat &frame, std::vector<uint8_t> &bits,
                       uint32_t messageLength, uint32_t seed) {
  cout << "Seed : " << seed << std::endl;
  std::mt19937 rng(seed); // Must match embedding RNG seed!

  int totalBits = messageLength * 8;
  for (int i = 0; i < totalBits; ++i) {
    int x = rng() % frame.cols;
    int y = rng() % frame.rows;

    uint8_t bit = frame.at<cv::Vec3b>(y, x)[0] & 1;
    bits.push_back(bit);
  }
}

void extractMessage(const VidOpts &o = VidOpts()) {
  string videoPath;
  if (o.interactive) {
    cout << "=== EXTRACT MODE ===\n";
    cout << "Enter steganographic video path: ";
    cin >> videoPath;
  } else {
    videoPath = o.inPath;
  }

  VideoCapture cap(videoPath);
  if (!cap.isOpened()) {
    cerr << "Error opening video.\n";
    return;
  }

  int totalFrames = static_cast<int>(cap.get(CAP_PROP_FRAME_COUNT));

  Mat frame;
  if (!cap.read(frame)) {
    cerr << "Failed to read first frame.\n";
    return;
  }

  uint8_t metadata = extractMetadata(frame);
  uint32_t messageLengthBits = extractLength(frame);
  cout << "Metadata : " << static_cast<int>(metadata) << std::endl;
  bool frameMode = (metadata >> 7) & 1;
  bool pixelMode = (metadata >> 6) & 1;
  bool encryptFlag = (metadata >> 5) & 1;
  uint32_t seed = metadata & 0x1F;

  cout << "[*] Metadata extracted:\n";
  cout << "Frame mode: " << (frameMode ? "Random" : "Sequential") << endl;
  cout << "Pixel mode: " << (pixelMode ? "Random" : "Sequential") << endl;
  cout << "Encryption: " << (encryptFlag ? "Yes" : "No") << endl;
  cout << "Message length: " << messageLengthBits << " bits ("
       << messageLengthBits / 8 << " bytes)\n";
  string key;
  if (encryptFlag) {
    if (o.interactive) {
      cout << "Enter key: ";
      cin >> key;
    } else {
      key = o.key;
    }
  }

  vector<bool> messageBits;
  extractMessageBits(messageBits, cap, messageLengthBits, pixelMode, frameMode,
                     totalFrames, seed);

  string message = bitsToMessage(messageBits);
  if (encryptFlag) {
    message = Vigenere::vigenere_decrypt(message, key);
  }

  if (encryptFlag) {
    cout << "[!] Encryption detected\n";
  }

  int saveMsg = 0;
  if (o.interactive) {
    cout << "Safe Message? 1.No, 2.Yes : ";
    cin >> saveMsg;
  } else {
    saveMsg = o.saveToFile ? 2 : 1;
  }

  cout << "\nExtracted Message:\n" << message << endl;
  if (saveMsg == 2) {
    string pathFile;
    if (o.interactive) {
      cout << "Enter path file to save : ";
      cin >> pathFile;
    } else {
      pathFile = o.outPath;
    }
    std::ofstream outFile(pathFile);
    // Check if the file opened successfully
    if (!outFile) {
      std::cerr << "Error opening file!" << std::endl;
    }

    // Write a line to the file
    outFile << message << std::endl;

    outFile.close();
  }

  cap.release();
}

// Fungsi untuk menghitung PSNR antara dua frame
double getPSNR(const Mat &I1, const Mat &I2) {
  if (I1.empty() || I2.empty()) {
    cerr << "Empty Frame!" << endl;
    return -1;
  }

  if (I1.size() != I2.size() || I1.type() != I2.type()) {
    cerr << "Error in frame size!" << endl;
    return -1;
  }

  Mat s1;
  absdiff(I1, I2, s1);
  s1.convertTo(s1, CV_32F);
  s1 = s1.mul(s1);

  Scalar s = sum(s1);

  double sse = s.val[0] + s.val[1] + s.val[2];

  if (sse <= 1e-10) {
    return INFINITY;
  } else {
    double mse = sse / (double)(I1.channels() * I1.total());
    double psnr = 10.0 * log10((255 * 255) / mse);
    return psnr;
  }
}

void checkPSNR() {

  VideoCapture capOriginal(original_path);
  VideoCapture capStego(stego_path);

  if (!capOriginal.isOpened() || !capStego.isOpened()) {
    cerr << "Failed to open one of the videos!" << endl;
  }

  int totalFrames = min((int)capOriginal.get(CAP_PROP_FRAME_COUNT),
                        (int)capStego.get(CAP_PROP_FRAME_COUNT));

  cout << "Total frames to analyze: " << totalFrames << endl;

  double totalPSNR = 0.0;
  int processedFrames = 0;

  for (int i = 0; i < totalFrames; ++i) {
    Mat frameOriginal, frameStego;

    capOriginal >> frameOriginal;
    capStego >> frameStego;

    if (frameOriginal.empty() || frameStego.empty()) {
      cout << "End of video at frame: " << i << endl;
      break;
    }

    double psnr = getPSNR(frameOriginal, frameStego);

    if (psnr >= 0 && psnr != INFINITY) {
      totalPSNR += psnr;
      processedFrames++;
      cout << "Frame " << i << ": PSNR = " << psnr << " dB" << endl;
    } else {
      cerr << "Error calculating PSNR at frame " << i << endl;
    }
  }

  if (processedFrames > 0) {
    double averagePSNR = totalPSNR / processedFrames;
    cout << "\n=== SUMMARY ===" << endl;
    cout << "Frames analyzed: " << processedFrames << endl;
    cout << "Average PSNR: " << averagePSNR << " dB" << endl;
  } else {
    cout << "No frames were processed!" << endl;
  }

  capOriginal.release();
  capStego.release();
}

// ====================== Main Program ======================
static void printVideoUsage(const char *prog) {
  cout << "StegoNinja video LSB tool\n\n"
       << "Interactive (menu):\n  " << prog << "\n\n"
       << "Non-interactive:\n"
       << "  " << prog
       << " embed --cover <in> (--message <text> | --message-file <f>) --output <out.avi>\n"
       << "        [--seed N] [--frame-mode seq|rand] [--pixel-mode seq|rand] [--key <k>]\n"
       << "  " << prog << " extract --stego <in.avi> [--output <file>] [--key <k>]\n\n"
       << "Notes:\n"
       << "  --key enables Vigenere obfuscation (NOT strong encryption).\n"
       << "  --seed must be 0..31 (only 5 bits are stored); embed and extract must match.\n"
       << "  The message is single-line text; output is a lossless FFV1/AVI.\n";
}

int main(int argc, char *argv[]) {
  // No arguments: keep the original interactive menu.
  if (argc == 1) {
    int mode;
    cout << "===== VIDEO STEGANOGRAPHY TOOL =====\n";
    cout << "Choose Mode:\n1. Embed Message\n2. Extract Message\nSelect: ";
    cin >> mode;
    if (mode == 1) {
      embedMessage();
      checkPSNR();
    } else if (mode == 2) {
      extractMessage();
    } else {
      cerr << "Invalid mode selected.\n";
    }
    return 0;
  }

  string cmd = argv[1];
  if (cmd == "-h" || cmd == "--help") {
    printVideoUsage(argv[0]);
    return 0;
  }
  if (cmd != "embed" && cmd != "extract") {
    cerr << "Unknown command: " << cmd << "\n";
    printVideoUsage(argv[0]);
    return 2;
  }

  VidOpts o;
  o.interactive = false;
  string messageFile;
  for (int i = 2; i < argc; ++i) {
    string a = argv[i];
    auto need = [&](const string &name) -> string {
      if (i + 1 >= argc) {
        cerr << "Missing value for " << name << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--cover" || a == "--stego" || a == "--in")
      o.inPath = need(a);
    else if (a == "--output" || a == "--out") {
      o.outPath = need(a);
      o.saveToFile = true;
    } else if (a == "--message")
      o.message = need(a);
    else if (a == "--message-file")
      messageFile = need(a);
    else if (a == "--key") {
      o.key = need(a);
      o.encryptFlag = 1;
    } else if (a == "--seed")
      o.seed = std::stoi(need(a));
    else if (a == "--frame-mode") {
      string v = need(a);
      o.frameMode = (v == "rand" || v == "random" || v == "2") ? 2 : 1;
    } else if (a == "--pixel-mode") {
      string v = need(a);
      o.pixelMode = (v == "rand" || v == "random" || v == "2") ? 2 : 1;
    } else {
      cerr << "Unknown option: " << a << "\n";
      printVideoUsage(argv[0]);
      return 2;
    }
  }

  if (o.seed < 0 || o.seed > 31) {
    cerr << "--seed must be in the range 0..31\n";
    return 2;
  }

  if (cmd == "embed") {
    if (!messageFile.empty()) {
      std::ifstream mf(messageFile);
      if (!mf) {
        cerr << "Cannot open message file: " << messageFile << "\n";
        return 1;
      }
      std::stringstream ss;
      ss << mf.rdbuf();
      o.message = ss.str();
      if (!o.message.empty() && o.message.back() == '\n')
        o.message.pop_back();
    }
    if (o.inPath.empty() || o.outPath.empty()) {
      cerr << "embed needs --cover and --output\n";
      return 2;
    }
    if (o.encryptFlag && o.key.empty()) {
      cerr << "--key must be non-empty when encrypting\n";
      return 2;
    }
    embedMessage(o);
  } else { // extract
    if (o.inPath.empty()) {
      cerr << "extract needs --stego\n";
      return 2;
    }
    extractMessage(o);
  }
  return 0;
}
