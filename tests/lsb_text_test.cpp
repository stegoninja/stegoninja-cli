// Engine-level test for the image text-LSB path (steg::imglsb text), including
// the format-magic guard and optional Vigenere obfuscation. These functions do
// not use ncurses, so they can be driven directly.
// Usage: lsb_text_test <work_dir>
#include "engine/imglsb.h"
#include <cstdio>
#include <opencv2/opencv.hpp>
#include <string>

int main(int argc, char **argv) {
  std::string dir = (argc > 1) ? argv[1] : ".";
  std::string cover = dir + "/cover.png";
  std::string stego = dir + "/stego.png";
  std::string stegoEnc = dir + "/stego_enc.png";

  // Deterministic noisy cover (ample LSB capacity, lossless PNG).
  cv::Mat img(128, 128, CV_8UC3);
  for (int y = 0; y < img.rows; ++y)
    for (int x = 0; x < img.cols; ++x)
      img.at<cv::Vec3b>(y, x) =
          cv::Vec3b((x * 7 + y * 13) % 256, (x * 5 + y * 3) % 256, (x ^ y) % 256);
  cv::imwrite(cover, img);

  int fails = 0;
  const std::string msg = "Hello LSB text guard 123!";

  // Plain round-trip.
  try {
    steg::imglsb::embedMessage(cover, stego, msg);
    std::string out = steg::imglsb::extractMessage(stego);
    if (out == msg)
      printf("  PASS  text round-trip\n");
    else {
      printf("  FAIL  text round-trip (out='%s')\n", out.c_str());
      ++fails;
    }
  } catch (const std::exception &e) {
    printf("  FAIL  text round-trip threw: %s\n", e.what());
    ++fails;
  }

  // Encrypted round-trip (payload may contain NUL bytes — length-prefixed frame
  // must survive that).
  try {
    steg::imglsb::embedMessage(cover, stegoEnc, msg, "k3y");
    std::string out = steg::imglsb::extractMessage(stegoEnc, "k3y");
    if (out == msg)
      printf("  PASS  encrypted text round-trip\n");
    else {
      printf("  FAIL  encrypted text round-trip (out='%s')\n", out.c_str());
      ++fails;
    }
  } catch (const std::exception &e) {
    printf("  FAIL  encrypted round-trip threw: %s\n", e.what());
    ++fails;
  }

  // Guard: extracting from a plain image (no magic) must be rejected.
  try {
    steg::imglsb::extractMessage(cover);
    printf("  FAIL  plain image not rejected by magic guard\n");
    ++fails;
  } catch (const std::exception &) {
    printf("  PASS  plain image rejected by magic guard\n");
  }

  // detect() should classify the text stego as Text and the plain cover as None.
  if (steg::imglsb::detect(stego) == steg::imglsb::Mode::Text)
    printf("  PASS  detect() identifies text stego\n");
  else {
    printf("  FAIL  detect() misidentified text stego\n");
    ++fails;
  }

  printf("%s\n", fails ? "text-LSB: FAILURES" : "text-LSB: all passed");
  return fails ? 2 : 0;
}
