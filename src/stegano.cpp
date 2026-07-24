#include "stegano.h"
#include "vigenere.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <ncurses.h>

bool Stegano::previewsEnabled() {
  if (std::getenv("STEGO_NO_PREVIEW") != nullptr)
    return false;
  if (std::getenv("DISPLAY") == nullptr)
    return false;
  return true;
}

bool Stegano::embedMessage(const std::string &inputImage,
                           const std::string &outputImage,
                           const std::string &message) {
  cv::Mat img = cv::imread(inputImage, cv::IMREAD_COLOR);
  if (img.empty()) {
    return false;
  }

  int msgLen = message.length();
  if (msgLen * 8 > img.rows * img.cols * 3) {
    return false; // message too big
  }

  int msgIndex = 0;
  int bitIndex = 0;

  for (int row = 0; row < img.rows && msgIndex < msgLen; ++row) {
    for (int col = 0; col < img.cols && msgIndex < msgLen; ++col) {
      for (int ch = 0; ch < img.channels(); ++ch) {
        uchar &pixel = img.at<cv::Vec3b>(row, col)[ch];
        pixel &= 0xFE; // clear LSB
        pixel |= ((message[msgIndex] >> bitIndex) & 1);
        ++bitIndex;
        if (bitIndex == 8) {
          bitIndex = 0;
          ++msgIndex;
        }
      }
    }
  }

  return cv::imwrite(outputImage, img);
}

bool Stegano::embedImage(const cv::Mat &carrier, const cv::Mat &secret,
                         const std::string &outputPath) {

  cv::Mat carrier_img = carrier.clone();
  cv::Mat original_img = carrier.clone();
  cv::Mat secret_img = secret.clone();

  // Encode secret image to a compressed PNG byte array
  std::vector<unsigned char> secretBuffer;
  cv::imencode(".png", secret, secretBuffer);
  // Total number of bits to embed
  size_t dataSize = secretBuffer.size();
  size_t totalBits = (8 * 8) + (dataSize * 8); // 8-byte header + data

  // Capacity: rows * cols * 3 channels
  size_t capacity = carrier_img.rows * carrier_img.cols * 3;

  getchar();
  clear();
  mvprintw(2, 1, "Cover capacity %d bytes\n", capacity);
  mvprintw(2, 1, "Secret image data + header: %d bytes\n", dataSize);
  refresh();

  if (totalBits > capacity) {
    mvprintw(3, 1,
             "Error: Secret image too big! Resize it or use a bigger "
             "cover image.");
    refresh();
    return false;
  }

  // Prepare data: first embed the payload size as a fixed 8-byte
  // little-endian header (portable across word size / endianness).
  std::vector<unsigned char> sizeBuffer(8);
  for (int i = 0; i < 8; ++i)
    sizeBuffer[i] = (static_cast<uint64_t>(dataSize) >> (8 * i)) & 0xFF;

  // Merge sizeBuffer and encryptedData into one vector
  std::vector<unsigned char> payload(sizeBuffer);
  payload.insert(payload.end(), secretBuffer.begin(), secretBuffer.end());

  // Start embedding bits
  size_t bitIndex = 0;
  for (int row = 0; row < carrier_img.rows; ++row) {
    for (int col = 0; col < carrier_img.cols; ++col) {
      cv::Vec3b &pixel = carrier_img.at<cv::Vec3b>(row, col);

      for (int channel = 0; channel < 3; ++channel) {
        if (bitIndex >= totalBits) {
          break;
        }

        size_t byteIndex = bitIndex / 8;
        int bitPosition = 7 - (bitIndex % 8);

        int bit = (payload[byteIndex] >> bitPosition) & 1;

        pixel[channel] &= 0xFE; // Clear LSB
        pixel[channel] |= bit;  // Set LSB

        ++bitIndex;
      }

      if (bitIndex >= totalBits) {
        break;
      }
    }

    if (bitIndex >= totalBits) {
      break;
    }
  }

  cv::Mat anotherCarrier = carrier_img.clone();
  // Save the output image
  if (!cv::imwrite(outputPath, carrier_img)) {
    std::cerr << "Failed to save output image!" << std::endl;
    return false;
  }

  mvprintw(5, 1, "Successfully embedded and saved to %s", outputPath.c_str());

  // Resize both images to the same height
  int height = std::max(anotherCarrier.rows, original_img.rows);
  cv::Mat stego_display;

  cv::resize(original_img, original_img, cv::Size(640, 480));
  cv::resize(anotherCarrier, anotherCarrier, cv::Size(640, 480));

  // Combine them side by side
  cv::Mat combined_image;
  cv::hconcat(original_img, anotherCarrier, combined_image);

  // ----- Add label "Original" -----
  std::string text1 = "Original";
  int fontFace = cv::FONT_HERSHEY_SIMPLEX;
  double fontScale = 1.0;
  int thickness = 2;

  int baseline1 = 0;
  cv::Size textSize1 =
      cv::getTextSize(text1, fontFace, fontScale, thickness, &baseline1);

  cv::Point textOrg1(10, 30);
  cv::Rect bgRect1(textOrg1 + cv::Point(0, baseline1), textSize1);

  cv::Mat roi1 = combined_image(bgRect1);
  cv::Mat overlay1;
  roi1.copyTo(overlay1);
  cv::rectangle(overlay1, cv::Point(0, 0), textSize1, cv::Scalar(0, 0, 0),
                cv::FILLED);

  double alpha = 0.5;
  cv::addWeighted(overlay1, alpha, roi1, 1.0 - alpha, 0, roi1);

  cv::putText(combined_image, text1, textOrg1 + cv::Point(0, textSize1.height),
              fontFace, fontScale, cv::Scalar(255, 255, 255), thickness);

  // ----- Add label "Stego Image" -----
  std::string text2 = "Stego Image";
  int baseline2 = 0;
  cv::Size textSize2 =
      cv::getTextSize(text2, fontFace, fontScale, thickness, &baseline2);

  cv::Point textOrg2(anotherCarrier.cols + 10, 30);
  cv::Rect bgRect2(textOrg2 + cv::Point(0, baseline2), textSize2);

  cv::Mat roi2 = combined_image(bgRect2);
  cv::Mat overlay2;
  roi2.copyTo(overlay2);
  cv::rectangle(overlay2, cv::Point(0, 0), textSize2, cv::Scalar(0, 0, 0),
                cv::FILLED);

  cv::addWeighted(overlay2, alpha, roi2, 1.0 - alpha, 0, roi2);

  cv::putText(combined_image, text2, textOrg2 + cv::Point(0, textSize2.height),
              fontFace, fontScale, cv::Scalar(255, 255, 255), thickness);

  // Show combined image (skipped when running headless)
  if (Stegano::previewsEnabled()) {
    cv::namedWindow("Side by Side Images", cv::WINDOW_AUTOSIZE);
    cv::imshow("Side by Side Images", combined_image);
    cv::waitKey(1000); // Process window events
    cv::imshow("Side by Side Images", combined_image);
  }
  mvprintw(4, 1, "Success to embed!\n");
  mvprintw(6, 1, "Press any key to continue...\n");
  refresh();
  getch();
  if (Stegano::previewsEnabled())
    cv::destroyAllWindows();
  return true;
}

// Embed data size and encrypted data into the carrier image
bool Stegano::embedImage(const cv::Mat &carrier, const cv::Mat &secret,
                         const std::string &key,
                         const std::string &outputPath) {
  cv::Mat carrier_img = carrier.clone();
  cv::Mat original_img = carrier.clone();
  cv::Mat secret_img = secret.clone();

  // Encode secret image to a compressed PNG byte array
  std::vector<unsigned char> secretBuffer;
  cv::imencode(".png", secret, secretBuffer);

  // Encrypt the data
  std::vector<unsigned char> encryptedData =
      Vigenere::vigenereEncrypt(secretBuffer, key);

  // Total number of bits to embed
  size_t dataSize = encryptedData.size();
  size_t totalBits = (8 * 8) + (dataSize * 8); // 8-byte header + data

  // Capacity: rows * cols * 3 channels
  size_t capacity = carrier_img.rows * carrier_img.cols * 3;

  clear();
  mvprintw(2, 1, "Cover capacity %d bytes\n", capacity);
  mvprintw(2, 1, "Secret image data + header: %d bytes\n", dataSize);
  refresh();

  if (totalBits > capacity) {
    mvprintw(3, 1,
             "Error: Secret image too big! Resize it or use a bigger "
             "cover image.");
    refresh();
    return false;
  }

  // Prepare data: first embed the payload size as a fixed 8-byte
  // little-endian header (portable across word size / endianness).
  std::vector<unsigned char> sizeBuffer(8);
  for (int i = 0; i < 8; ++i)
    sizeBuffer[i] = (static_cast<uint64_t>(dataSize) >> (8 * i)) & 0xFF;

  // Merge sizeBuffer and encryptedData into one vector
  std::vector<unsigned char> payload(sizeBuffer);
  payload.insert(payload.end(), encryptedData.begin(), encryptedData.end());

  // Start embedding bits
  size_t bitIndex = 0;
  for (int row = 0; row < carrier_img.rows; ++row) {
    for (int col = 0; col < carrier_img.cols; ++col) {
      cv::Vec3b &pixel = carrier_img.at<cv::Vec3b>(row, col);

      for (int channel = 0; channel < 3; ++channel) {
        if (bitIndex >= totalBits) {
          break;
        }

        size_t byteIndex = bitIndex / 8;
        int bitPosition = 7 - (bitIndex % 8);

        int bit = (payload[byteIndex] >> bitPosition) & 1;

        pixel[channel] &= 0xFE; // Clear LSB
        pixel[channel] |= bit;  // Set LSB

        ++bitIndex;
      }

      if (bitIndex >= totalBits) {
        break;
      }
    }

    if (bitIndex >= totalBits) {
      break;
    }
  }

  cv::Mat anotherCarrier = carrier_img.clone();
  // Save the output image
  if (!cv::imwrite(outputPath, carrier_img)) {
    std::cerr << "Failed to save output image!" << std::endl;
    return false;
  }

  mvprintw(5, 1, "Successfully embedded and saved to %s", outputPath.c_str());

  // Resize both images to the same height
  int height = std::max(anotherCarrier.rows, original_img.rows);
  cv::Mat stego_display;

  cv::resize(original_img, original_img, cv::Size(640, 480));
  cv::resize(anotherCarrier, anotherCarrier, cv::Size(640, 480));

  // Combine them side by side
  cv::Mat combined_image;
  cv::hconcat(original_img, anotherCarrier, combined_image);

  // ----- Add label "Original" -----
  std::string text1 = "Original";
  int fontFace = cv::FONT_HERSHEY_SIMPLEX;
  double fontScale = 1.0;
  int thickness = 2;

  int baseline1 = 0;
  cv::Size textSize1 =
      cv::getTextSize(text1, fontFace, fontScale, thickness, &baseline1);

  cv::Point textOrg1(10, 30);
  cv::Rect bgRect1(textOrg1 + cv::Point(0, baseline1), textSize1);

  cv::Mat roi1 = combined_image(bgRect1);
  cv::Mat overlay1;
  roi1.copyTo(overlay1);
  cv::rectangle(overlay1, cv::Point(0, 0), textSize1, cv::Scalar(0, 0, 0),
                cv::FILLED);

  double alpha = 0.5;
  cv::addWeighted(overlay1, alpha, roi1, 1.0 - alpha, 0, roi1);

  cv::putText(combined_image, text1, textOrg1 + cv::Point(0, textSize1.height),
              fontFace, fontScale, cv::Scalar(255, 255, 255), thickness);

  // ----- Add label "Stego Image" -----
  std::string text2 = "Stego Image";
  int baseline2 = 0;
  cv::Size textSize2 =
      cv::getTextSize(text2, fontFace, fontScale, thickness, &baseline2);

  cv::Point textOrg2(anotherCarrier.cols + 10, 30);
  cv::Rect bgRect2(textOrg2 + cv::Point(0, baseline2), textSize2);

  cv::Mat roi2 = combined_image(bgRect2);
  cv::Mat overlay2;
  roi2.copyTo(overlay2);
  cv::rectangle(overlay2, cv::Point(0, 0), textSize2, cv::Scalar(0, 0, 0),
                cv::FILLED);

  cv::addWeighted(overlay2, alpha, roi2, 1.0 - alpha, 0, roi2);

  cv::putText(combined_image, text2, textOrg2 + cv::Point(0, textSize2.height),
              fontFace, fontScale, cv::Scalar(255, 255, 255), thickness);

  // Show combined image (skipped when running headless)
  if (Stegano::previewsEnabled()) {
    cv::namedWindow("Side by Side Images", cv::WINDOW_AUTOSIZE);
    cv::imshow("Side by Side Images", combined_image);
    cv::waitKey(1000); // Process window events
    cv::imshow("Side by Side Images", combined_image);
  }
  mvprintw(4, 1, "Success to embed!\n");
  mvprintw(6, 1, "Press any key to continue...\n");
  refresh();
  getch();
  if (Stegano::previewsEnabled())
    cv::destroyAllWindows();
  return true;
}

bool Stegano::extractMessage(const std::string &inputImage,
                             std::string &message) {
  cv::Mat img = cv::imread(inputImage, cv::IMREAD_COLOR);
  if (img.empty()) {
    return false;
  }

  std::string extracted;
  char ch = 0;
  int bitIndex = 0;

  for (int row = 0; row < img.rows; ++row) {
    for (int col = 0; col < img.cols; ++col) {
      for (int c = 0; c < img.channels(); ++c) {
        uchar pixel = img.at<cv::Vec3b>(row, col)[c];
        ch |= (pixel & 1) << bitIndex;
        ++bitIndex;
        if (bitIndex == 8) {
          if (ch == '\0') {
            message = extracted;
            return true;
          }
          extracted += ch;
          ch = 0;
          bitIndex = 0;
        }
      }
    }
  }

  message = extracted;
  return true;
}

bool Stegano::embedData(const cv::Mat &coverImage,
                        const std::vector<unsigned char> &data,
                        cv::Mat &stegoImage) {

  int totalBits = data.size() * 8;
  int maxCapacity = coverImage.rows * coverImage.cols * 3;

  if (totalBits > maxCapacity) {
    std::cout << "Error: Data too big to embed! Max bytes = "
              << (maxCapacity / 8) << std::endl;
    return false;
  }

  stegoImage = coverImage.clone();

  int bitIndex = 0;
  for (int row = 0; row < coverImage.rows && bitIndex < totalBits; row++) {
    for (int col = 0; col < coverImage.cols && bitIndex < totalBits; col++) {
      cv::Vec3b pixel = stegoImage.at<cv::Vec3b>(row, col);

      for (int channel = 0; channel < 3 && bitIndex < totalBits; channel++) {
        int byteIndex = bitIndex / 8;
        int bitInByte = 7 - (bitIndex % 8);
        unsigned char bit = (data[byteIndex] >> bitInByte) & 1;

        pixel[channel] = (pixel[channel] & 0xFE) | bit;
        bitIndex++;
      }

      stegoImage.at<cv::Vec3b>(row, col) = pixel;
    }
  }

  return true;
}

// Helper: Convert int (4 bytes) to 4 bytes vector
std::vector<unsigned char> Stegano::intToBytes(int value) {
  std::vector<unsigned char> bytes(4);
  bytes[0] = (value >> 24) & 0xFF;
  bytes[1] = (value >> 16) & 0xFF;
  bytes[2] = (value >> 8) & 0xFF;
  bytes[3] = value & 0xFF;
  return bytes;
}

// Helper: Convert 4 bytes vector to int
int Stegano::bytesToInt(const std::vector<unsigned char> &bytes) {
  return (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
}

// Extract encrypted data and decrypt to recover the hidden image
bool Stegano::extractImage(const cv::Mat &carrier_img, const std::string &key,
                           const std::string &outputPath) {
  size_t bitIndex = 0;

  // First extract the payload size (fixed 8-byte little-endian header).
  size_t dataSize = 0;
  std::vector<unsigned char> sizeBuffer(8);

  for (size_t i = 0; i < sizeBuffer.size(); ++i) {
    unsigned char currentByte = 0;

    for (int bitPos = 7; bitPos >= 0; --bitPos) {
      int row = bitIndex / (carrier_img.cols * 3);
      int col = (bitIndex / 3) % carrier_img.cols;
      int channel = bitIndex % 3;

      cv::Vec3b pixel = carrier_img.at<cv::Vec3b>(row, col);
      int lsb = pixel[channel] & 1;

      currentByte |= (lsb << bitPos);

      ++bitIndex;
    }

    sizeBuffer[i] = currentByte;
  }

  mvprintw(3, 1, "Selected path:\n");
  refresh();
  getch();
  dataSize = 0;
  for (int i = 0; i < 8; ++i)
    dataSize |= static_cast<size_t>(sizeBuffer[i]) << (8 * i);
  // std::cout << "Data size to extract: " << dataSize << " bytes" << std::endl;

  // Now extract the actual encrypted data
  std::vector<unsigned char> encryptedData(dataSize);

  for (size_t i = 0; i < encryptedData.size(); ++i) {
    unsigned char currentByte = 0;

    for (int bitPos = 7; bitPos >= 0; --bitPos) {
      int row = bitIndex / (carrier_img.cols * 3);
      int col = (bitIndex / 3) % carrier_img.cols;
      int channel = bitIndex % 3;

      cv::Vec3b pixel = carrier_img.at<cv::Vec3b>(row, col);
      int lsb = pixel[channel] & 1;

      currentByte |= (lsb << bitPos);

      ++bitIndex;
    }

    encryptedData[i] = currentByte;
  }

  // Decrypt data
  std::vector<unsigned char> decryptedData =
      Vigenere::vigenereDecrypt(encryptedData, key);

  // Decode the image
  cv::Mat extracted_img = cv::imdecode(decryptedData, cv::IMREAD_UNCHANGED);

  if (extracted_img.empty()) {
    mvprintw(5, 1, "Failed to decode extracted image!\n");

    mvprintw(6, 1, "Press any key to continue... \n");
    refresh();
    getch();
    return false;
  }
  cv::imwrite(outputPath, extracted_img);

  mvprintw(5, 1, "Successfully extracted and saved to %s", outputPath.c_str());

  mvprintw(6, 1, "Press any key to continue... \n");
  refresh();
  getch();
  return true;
}

bool Stegano::extractImage(const cv::Mat &carrier_img,
                           const std::string &outputPath) {
  size_t bitIndex = 0;

  // First extract the payload size (fixed 8-byte little-endian header).
  size_t dataSize = 0;
  std::vector<unsigned char> sizeBuffer(8);

  for (size_t i = 0; i < sizeBuffer.size(); ++i) {
    unsigned char currentByte = 0;

    for (int bitPos = 7; bitPos >= 0; --bitPos) {
      int row = bitIndex / (carrier_img.cols * 3);
      int col = (bitIndex / 3) % carrier_img.cols;
      int channel = bitIndex % 3;

      cv::Vec3b pixel = carrier_img.at<cv::Vec3b>(row, col);
      int lsb = pixel[channel] & 1;

      currentByte |= (lsb << bitPos);

      ++bitIndex;
    }

    sizeBuffer[i] = currentByte;
  }

  mvprintw(3, 1, "Selected path:\n");
  refresh();
  getch();
  dataSize = 0;
  for (int i = 0; i < 8; ++i)
    dataSize |= static_cast<size_t>(sizeBuffer[i]) << (8 * i);
  // std::cout << "Data size to extract: " << dataSize << " bytes" << std::endl;

  // Now extract the actual encrypted data
  std::vector<unsigned char> encryptedData(dataSize);

  for (size_t i = 0; i < encryptedData.size(); ++i) {
    unsigned char currentByte = 0;

    for (int bitPos = 7; bitPos >= 0; --bitPos) {
      int row = bitIndex / (carrier_img.cols * 3);
      int col = (bitIndex / 3) % carrier_img.cols;
      int channel = bitIndex % 3;

      cv::Vec3b pixel = carrier_img.at<cv::Vec3b>(row, col);
      int lsb = pixel[channel] & 1;

      currentByte |= (lsb << bitPos);

      ++bitIndex;
    }

    encryptedData[i] = currentByte;
  }

  // Decode the image
  cv::Mat extracted_img = cv::imdecode(encryptedData, cv::IMREAD_UNCHANGED);

  if (extracted_img.empty()) {
    mvprintw(5, 1, "Failed to decode extracted image!\n");

    mvprintw(6, 1, "Press any key to continue... \n");
    refresh();
    getch();
    return false;
  }
  cv::imwrite(outputPath, extracted_img);

  mvprintw(5, 1, "Successfully extracted and saved to %s", outputPath.c_str());

  mvprintw(6, 1, "Press any key to continue... \n");
  refresh();
  getch();
  return true;
}
