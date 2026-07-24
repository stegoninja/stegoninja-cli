// End-to-end test for the image text-LSB path (Stegano::embedMessage /
// extractMessage), including the format-magic guard. These functions do not use
// ncurses, so they can be driven directly. Usage: lsb_text_test <work_dir>
#include "stegano.h"
#include <cstdio>
#include <opencv2/opencv.hpp>
#include <string>

int main(int argc, char **argv) {
  std::string dir = (argc > 1) ? argv[1] : ".";
  std::string cover = dir + "/cover.png";
  std::string stego = dir + "/stego.png";

  // Deterministic noisy cover (ample LSB capacity, lossless PNG).
  cv::Mat img(128, 128, CV_8UC3);
  for (int y = 0; y < img.rows; ++y)
    for (int x = 0; x < img.cols; ++x)
      img.at<cv::Vec3b>(y, x) =
          cv::Vec3b((x * 7 + y * 13) % 256, (x * 5 + y * 3) % 256, (x ^ y) % 256);
  cv::imwrite(cover, img);

  int fails = 0;

  // main.cpp appends a NUL terminator before calling embedMessage.
  const std::string msg = "Hello LSB text guard 123!";
  std::string input = msg;
  input.push_back('\0');

  if (!Stegano::embedMessage(cover, stego, input)) {
    printf("  FAIL  embed returned false\n");
    return 1;
  }

  std::string out;
  bool ok = Stegano::extractMessage(stego, out);
  if (ok && out == msg)
    printf("  PASS  text round-trip\n");
  else {
    printf("  FAIL  text round-trip (ok=%d out='%s')\n", ok, out.c_str());
    ++fails;
  }

  // Guard: extracting from a plain image (no magic) must be rejected.
  std::string out2;
  bool ok2 = Stegano::extractMessage(cover, out2);
  if (!ok2)
    printf("  PASS  plain image rejected by magic guard\n");
  else {
    printf("  FAIL  plain image not rejected (out='%s')\n", out2.c_str());
    ++fails;
  }

  printf("%s\n", fails ? "text-LSB: FAILURES" : "text-LSB: all passed");
  return fails ? 2 : 0;
}
