#include "stegano.h"
#include "vigenere.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <ncurses.h>
#include <opencv2/features2d.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include <ncurses.h>
#include <opencv2/opencv.hpp>
#include <thread>

#include <dirent.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_FILES 1000
#define PATH_MAX 4096
#define MAX_NAME_DISPLAY 30

typedef struct {
  char name[PATH_MAX];
  int is_dir;
} FileEntry;

// Global Variable
FileEntry files[MAX_FILES];
int selected = 0;
int file_count = 0;
char current_dir[PATH_MAX]; // Buffer to hold the current directory path

namespace fs = std::filesystem;

// Function to display an image using OpenCV
void show_image(const std::string &image_path) {
  cv::Mat image = cv::imread(image_path);
  if (image.empty()) {
    printw("Error: Could not open image.\n");
    return;
  }
  if (!Stegano::previewsEnabled())
    return;
  cv::imshow("Image Viewer", image);
  cv::waitKey(0); // Wait for a key press to close the window
  cv::destroyAllWindows();
}

void truncate_name(char *display_name, const char *original_name) {
  int max_len = MAX_NAME_DISPLAY;
  int original_len = strlen(original_name);

  if (original_len <= max_len) {
    strncpy(display_name, original_name, max_len);
    display_name[max_len] = '\0'; // Ensure null-termination
  } else {
    strncpy(display_name, original_name, max_len - 3); // Leave room for "..."
    display_name[max_len - 3] = '\0'; // Ensure null-termination
    strcat(display_name, "...");
  }
}

double getPSNR(const cv::Mat &I1, const cv::Mat &I2) {
  cv::Mat s1;
  absdiff(I1, I2, s1);      // |I1 - I2|
  s1.convertTo(s1, CV_32F); // make sure it's float
  s1 = s1.mul(s1);          // square the difference

  cv::Scalar s = sum(s1); // sum all elements per channel

  double sse = s.val[0] + s.val[1] + s.val[2]; // sum channels

  if (sse <= 1e-10) { // if small enough -> identical images
    return INFINITY;
  } else {
    double mse = sse / (double)(I1.channels() * I1.total());
    double psnr = 10.0 * log10((255 * 255) / mse);
    return psnr;
  }
}

// Function to draw the TUI interface in a grid layout
void draw_interface() {
  clear();
  int max_y, max_x;
  getmaxyx(stdscr, max_y, max_x); // Get terminal dimensions

  printw("Current Directory: %s\n", current_dir);
  printw("Use arrow keys to navigate, Enter to open, 'q' to quit.\n\n");

  // Calculate the maximum displayed file name length
  int max_display_length =
      MAX_NAME_DISPLAY + 6; // +6 for [DIR] or [FILE] prefix

  // Calculate the number of columns and rows
  int cols = max_x / (max_display_length + 2); // +2 for spacing
  int rows = (file_count + cols - 1) / cols;

  // Display files in a grid
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      int index = row * cols + col;
      if (index >= file_count) {
        break;
      }

      int x = col * (max_display_length + 2);
      int y = row + 3; // Offset for header

      // Truncate the file name for display
      char display_name[MAX_NAME_DISPLAY +
                        4]; // +4 for "..." and null terminator
      truncate_name(display_name, files[index].name);

      if (index == selected) {
        attron(A_REVERSE);
      }
      mvprintw(y, x, "%s %s", files[index].is_dir ? "[DIR]" : "[FILE]",
               display_name);
      if (index == selected) {
        attroff(A_REVERSE);
      }
    }
  }

  refresh();
}

void list_files(const char *path) {
  DIR *dir;
  struct dirent *ent;
  file_count = 0;

  if ((dir = opendir(path)) != NULL) {
    while ((ent = readdir(dir)) != NULL && file_count < MAX_FILES) {
      if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
        // Safely copy the file name into the buffer
        strncpy(files[file_count].name, ent->d_name, PATH_MAX - 1);
        files[file_count].name[PATH_MAX - 1] = '\0'; // Ensure
        files[file_count].is_dir = (ent->d_type == DT_DIR);
        file_count++;
      }
    }
    closedir(dir);
  } else {
    printw("Cannot open directory: %s\n", path);
  }
}

std::string get_file_path() {
  std::string file_path = "None";

  if (getcwd(current_dir, PATH_MAX) == NULL) {
    printw("Error: Could not get current directory.\n");
    endwin();
    return file_path;
  }

  list_files(current_dir);

  int ch;

  draw_interface();

  while ((ch = getch()) != 'q') {
    switch (ch) {
    case KEY_UP: {
      int max_x, max_y;
      getmaxyx(stdscr, max_y, max_x);
      int cols = max_x / (MAX_NAME_DISPLAY + 8);
      selected = std::max(selected - cols, 0);
      break;
    }
    case KEY_DOWN: {
      int max_x, max_y;
      getmaxyx(stdscr, max_y, max_x);
      int cols = max_x / (MAX_NAME_DISPLAY + 8);
      selected = std::min(selected + cols, file_count - 1);
      break;
    }
    case KEY_LEFT:
      selected = std::max(selected - 1, 0);
      break;
    case KEY_RIGHT:
      selected = std::min(selected + 1, file_count - 1);
      break;
    case '\n': // Enter key
      if (files[selected].is_dir) {
        // Safely construct the new directory path
        char new_dir[PATH_MAX];
        snprintf(new_dir, PATH_MAX, "%s/%s", current_dir, files[selected].name);
        strncpy(current_dir, new_dir, PATH_MAX - 1);
        current_dir[PATH_MAX - 1] = '\0'; // Ensure null-termination
        list_files(current_dir);
        selected = 0;
      } else {
        // Check if the file is an image (simple extension check)
        const char *filename = files[selected].name;
        const char *ext = strrchr(filename, '.');
        if (ext && (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".png") == 0 ||
                    strcmp(ext, ".bmp") == 0)) {
          // Build the full path to the image
          char image_path[PATH_MAX];
          snprintf(image_path, PATH_MAX, "%s/%s", current_dir, filename);

          // Launch a thread to display the image
          // std::thread image_thread(show_image, image_path);
          // image_thread.detach(); // Detach the thread to run independently
          return image_path;
        } else {
          printw("Not an image file: %s\n", filename);
        }
      }
      break;
    case KEY_BACKSPACE:
    case 127: // Backspace key
      // Go up one directory level
      char *last_slash = strrchr(current_dir, '/');
      if (last_slash != NULL) {
        *last_slash = '\0';
        // Handle root directory case
        if (strlen(current_dir) == 0) {
          strcpy(current_dir, "/");
        }
        list_files(current_dir);
        selected = 0;
      }
      break;
    }
    draw_interface();
  }

  return file_path;
}

void embedFlow(std::string path, std::string saveAs) {
  clear();
  mvprintw(1, 1, "Embed Message");

  std::string inputImage = path;
  if (inputImage.empty())
    return;

  std::string outputImage = saveAs;

  char message[1024], key[26];
  int encryptChoice = 0;

  echo();
  mvprintw(5, 1, "Message to embed: ");
  getstr(message);

  mvprintw(6, 1, "Encrypt? [Vigenere = obfuscation, NOT strong crypto] (1=Yes,0=No): ");
  scanw("%d", &encryptChoice);

  std::string finalMessage = message;

  if (encryptChoice == 1) {
    mvprintw(7, 1, "Enter encryption key (max 25 chars): ");
    getstr(key);
    finalMessage = Vigenere::vigenere_encrypt(finalMessage, key);
  }
  noecho();

  finalMessage.push_back('\0');

  if (Stegano::embedMessage(inputImage, outputImage, finalMessage)) {
    mvprintw(9, 1, "Message embedded successfully!");
  } else {
    mvprintw(9, 1, "Failed to embed message!");
  }

  cv::Mat inputMat = cv::imread(inputImage);
  cv::Mat outputMat = cv::imread(outputImage);

  mvprintw(11, 1, "Source Image: %s", inputImage.c_str());
  mvprintw(12, 1, "  Size: %dx%d", inputMat.cols, inputMat.rows);
  mvprintw(14, 1, "Stego Image: %s", outputImage.c_str());
  mvprintw(15, 1, "  Size: %dx%d", outputMat.cols, outputMat.rows);

  // Resize both images to the same height
  int height = std::max(inputMat.rows, outputMat.rows);
  cv::resize(inputMat, inputMat,
             cv::Size(inputMat.cols * height / inputMat.rows, height));
  cv::resize(outputMat, outputMat,
             cv::Size(outputMat.cols * height / outputMat.rows, height));

  // Combine them side by side
  cv::Mat combined_image;
  cv::hconcat(inputMat, outputMat, combined_image);

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

  cv::Point textOrg2(inputMat.cols + 10, 30);
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
  mvprintw(17, 1, "Press any key to continue...");
  getch();
  if (Stegano::previewsEnabled())
    cv::destroyAllWindows();
}

void extractFlow(std::string path) {
  clear();
  mvprintw(1, 1, "Extract Message");

  std::string inputImage = path;
  if (inputImage.empty())
    return;

  std::string extractedMessage;
  int decryptChoice = 0;
  char key[26];

  if (!Stegano::extractMessage(inputImage, extractedMessage)) {
    mvprintw(5, 1, "Failed to extract message!");
  } else {
    mvprintw(5, 1, "Is message Encrypted? (1 = Yes, 0 = No): ");
    echo();
    char choices[4]; // Leave space for null-terminator!
    getstr(choices); // Get the input (expecting 1 char input)
    noecho();
    decryptChoice = static_cast<int>(choices[0]);

    if (decryptChoice == 49) {
      mvprintw(6, 1, "Enter decryption key: ");
      echo();
      getstr(key);
      noecho();

      extractedMessage = Vigenere::vigenere_decrypt(extractedMessage, key);
    }

    mvprintw(8, 1, "Extracted message: %s", extractedMessage.c_str());
  }

  cv::Mat inputMat = cv::imread(inputImage);

  mvprintw(10, 1, "Stego Image: %s", inputImage.c_str());
  mvprintw(11, 1, "  Size: %dx%d", inputMat.cols, inputMat.rows);

  mvprintw(13, 1, "Press any key to continue...");
  getch();
}

// Extract data from stego image
std::vector<unsigned char> extractData(const cv::Mat &stegoImage,
                                       int dataSizeInBytes) {
  std::vector<unsigned char> extracted(dataSizeInBytes, 0);

  int totalBits = dataSizeInBytes * 8;
  int bitIndex = 0;

  for (int row = 0; row < stegoImage.rows && bitIndex < totalBits; row++) {
    for (int col = 0; col < stegoImage.cols && bitIndex < totalBits; col++) {
      cv::Vec3b pixel = stegoImage.at<cv::Vec3b>(row, col);

      for (int channel = 0; channel < 3 && bitIndex < totalBits; channel++) {
        int byteIndex = bitIndex / 8;
        int bitInByte = 7 - (bitIndex % 8);

        unsigned char bit = pixel[channel] & 1;

        extracted[byteIndex] |= (bit << bitInByte);
        bitIndex++;
      }
    }
  }

  return extracted;
}

int main() {
  initscr();
  noecho();
  cbreak();
  keypad(stdscr, TRUE);

  int choice = 0;
  int highlight = 0;
  const char *choices[] = {"Embed Message", "Embed Image", "Extract Image",
                           "Extract Message", "Exit"};
  int numChoices = sizeof(choices) / sizeof(choices[0]);

  while (1) {
    clear();
    mvprintw(1, 1, "Steganography Tool");
    mvprintw(2, 1, "Use arrow keys to navigate. Press Enter to select.");

    for (int i = 0; i < numChoices; ++i) {
      if (i == highlight)
        attron(A_REVERSE);
      mvprintw(4 + i, 2, choices[i]);
      if (i == highlight)
        attroff(A_REVERSE);
    }

    int c = getch();

    switch (c) {
    case KEY_UP:
      highlight = (highlight == 0) ? numChoices - 1 : highlight - 1;
      break;
    case KEY_DOWN:
      highlight = (highlight == numChoices - 1) ? 0 : highlight + 1;
      break;

    case 10: // Enter
      choice = highlight;
      if (choice == 0) {
        std::string file_path = get_file_path();
        if (file_path == "None")
          continue;
        char saveAs[50];
        clear();
        echo();
        mvprintw(2, 1, "Enter save as name (max 50 chars): ");
        getstr(saveAs);
        embedFlow(file_path, saveAs);

        cv::Mat stego = cv::imread(saveAs);
        cv::Mat ori = cv::imread(file_path);

        clear();

        mvprintw(1, 1, "Scoring Menu\n");
        mvprintw(2, 1, "PSNR score %f \n", getPSNR(stego, ori));

        refresh();
      } else if (choice == 1) {
        clear();
        mvprintw(1, 1, "Choose Payload Image");
        mvprintw(2, 1, "Press any key to continue ...");
        getch();
        std::string payload_path = get_file_path();
        clear();
        mvprintw(1, 1, "Choose Embedded Image");
        mvprintw(2, 1, "Press any key to continue ...");
        getch();
        std::string embedded_path = get_file_path();
        cv::Mat payload_img = cv::imread(payload_path);

        if (payload_img.empty()) {
          std::cout << "Error: Cover image not found!" << std::endl;
          exit(EXIT_FAILURE);
          return -1;
        }
        cv::Mat embedded_img = cv::imread(embedded_path);

        std::cout << "embedded_img.type(): " << embedded_img.type()
                  << std::endl;
        if (embedded_img.empty()) {
          std::cout << "Error: Secret image not found!" << std::endl;
          exit(EXIT_FAILURE);
          return -1;
        }
        clear();
        char saveAs[50];
        echo();
        mvprintw(2, 1, "Enter save as name (max 50 chars): ");
        getstr(saveAs);

        clear();
        int encryptChoice = 0;
        // Confirmed if it's want to be encrypted or not
        echo();
        mvprintw(1, 1, "Encrypt? [Vigenere = obfuscation, NOT strong crypto] (1=Yes,0=No): ");
        scanw("%d", &encryptChoice);

        char key[26];

        if (encryptChoice) {

          mvprintw(2, 1, "Enter encryption key (max 25 chars): ");
          getstr(key);

          mvprintw(3, 1, "Your key : %s", key);
          refresh();
          getchar();

          bool success =
              Stegano::embedImage(payload_img, embedded_img, key, saveAs);

          if (!success) {
            mvprintw(5, 1, "Failed to embed\n");
            refresh();
            endwin();
          }
        } else {

          // no random key
          // using no key give crashes
          bool success =
              Stegano::embedImage(payload_img, embedded_img, "hello", saveAs);

          if (!success) {
            mvprintw(5, 1, "Failed to embed\n");
            refresh();
            endwin();
          }
          cv::Mat stego = cv::imread(saveAs);

          clear();

          mvprintw(1, 1, "Scoring Menu\n");
          mvprintw(2, 1, "PSNR score %f \n", getPSNR(stego, payload_img));
          refresh();
        }
      } else if (choice == 2) {
        std::string file_path = get_file_path();
        if (file_path == "None")
          continue;

        clear();
        int encryptChoice = 0;
        // Confirmed if it's want to be encrypted or not
        echo();
        mvprintw(1, 1, "Encrypt? [Vigenere = obfuscation, NOT strong crypto] (1=Yes,0=No): ");
        scanw("%d", &encryptChoice);

        char key[26];

        if (encryptChoice) {

          mvprintw(2, 1, "Enter encryption key (max 25 chars): ");
          getstr(key);

          cv::Mat inputImg = cv::imread(file_path);
          if (inputImg.empty()) {
            mvprintw(3, 1, "File Empty %s\n", file_path.c_str());
            refresh();
            continue;
          }

          mvprintw(3, 1, "Selected path: %s\n", file_path.c_str());
          mvprintw(4, 1, "Press any key to continue... \n");
          refresh();
          getch();

          bool success = Stegano::extractImage(inputImg, key, "extracted.png");
          if (success) {
            mvprintw(7, 1, "Extract Success\n");
            mvprintw(8, 1, "Press any key to continue... \n");
            refresh();
            getch();
          }
        } else {

          cv::Mat inputImg = cv::imread(file_path);
          if (inputImg.empty()) {
            mvprintw(3, 1, "File Empty %s\n", file_path.c_str());
            refresh();
            continue;
          }

          mvprintw(3, 1, "Selected path: %s\n", file_path.c_str());
          mvprintw(4, 1, "Press any key to continue... \n");
          refresh();
          getch();

          bool success =
              Stegano::extractImage(inputImg, "hello", "extracted.png");
          if (success) {
            mvprintw(7, 1, "Extract Success\n");
            mvprintw(8, 1, "Press any key to continue... \n");
            refresh();
            getch();
          }
        }
      } else if (choice == 3) {
        std::string file_path = get_file_path();
        if (file_path == "None")
          continue;
        extractFlow(file_path);
      } else
        goto end;
      break;
    default:
      break;
    }
  }

end:
  endwin();
  return 0;
}
