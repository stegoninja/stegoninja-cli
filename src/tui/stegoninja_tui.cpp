// stegoninja-tui — unified interactive front-end.
//
// One binary, one steganography engine, four techniques. An ncurses menu
// selects a technique (Image LSB, Image BPCS, Audio, Video) then embed/extract,
// guiding the user through inputs with an arrow-key file browser and, for image
// embeds, an OpenCV cover-vs-stego preview + PSNR (skipped when headless).
#include "engine/engine.h"

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <limits>
#include <ncurses.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

// ---- basic prompts --------------------------------------------------------
void pauseMsg(const std::string &msg, int row = 3) {
  mvprintw(row, 1, "%s", msg.c_str());
  mvprintw(row + 2, 1, "Press any key to continue...");
  refresh();
  getch();
}

std::string promptLine(int row, const std::string &label) {
  mvprintw(row, 1, "%s", label.c_str());
  refresh();
  echo();
  char buf[1024];
  buf[0] = '\0';
  getnstr(buf, sizeof(buf) - 1);
  noecho();
  return std::string(buf);
}

bool promptYesNo(int row, const std::string &label) {
  std::string v = promptLine(row, label + " (y/N): ");
  return !v.empty() && (v[0] == 'y' || v[0] == 'Y' || v[0] == '1');
}

// Optional key/password: prompt only if the user opts into encryption. Returns
// "" when encryption is declined. Always shows the honesty caveat.
std::string promptOptionalKey(int row) {
  if (!promptYesNo(row, "Encrypt? [Vigenere = obfuscation, NOT strong crypto]"))
    return "";
  std::string k = promptLine(row + 1, "Enter key: ");
  return k;
}

// ---- file browser (generalized over allowed extensions) -------------------
struct Entry {
  std::string name;
  bool isDir;
};

bool hasExt(const std::string &name, const std::vector<std::string> &exts) {
  if (exts.empty())
    return true;
  auto dot = name.find_last_of('.');
  if (dot == std::string::npos)
    return false;
  std::string e = name.substr(dot);
  std::transform(e.begin(), e.end(), e.begin(), ::tolower);
  return std::find(exts.begin(), exts.end(), e) != exts.end();
}

std::vector<Entry> listDir(const std::string &path) {
  std::vector<Entry> out;
  DIR *dir = opendir(path.c_str());
  if (!dir)
    return out;
  struct dirent *ent;
  while ((ent = readdir(dir)) != nullptr) {
    std::string n = ent->d_name;
    if (n == ".")
      continue;
    out.push_back({n, ent->d_type == DT_DIR});
  }
  closedir(dir);
  std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
    if (a.isDir != b.isDir)
      return a.isDir > b.isDir;
    return a.name < b.name;
  });
  return out;
}

// Returns a selected file path, or "" if the user quits with 'q'. `exts` filters
// which files are selectable (empty => any). Directories are always navigable.
std::string browseForFile(const std::string &title,
                          const std::vector<std::string> &exts) {
  char cwd[4096];
  if (!getcwd(cwd, sizeof(cwd)))
    return "";
  std::string dir = cwd;
  int sel = 0;
  auto entries = listDir(dir);

  while (true) {
    clear();
    mvprintw(0, 1, "%s", title.c_str());
    mvprintw(1, 1, "Dir: %s", dir.c_str());
    mvprintw(2, 1, "Up/Down select, Enter open/pick, Backspace up, q cancel");
    int maxy, maxx;
    getmaxyx(stdscr, maxy, maxx);
    (void)maxx;
    int rows = maxy - 5;
    int top = std::max(0, sel - rows + 1);
    for (int i = 0; i < rows && top + i < (int)entries.size(); ++i) {
      int idx = top + i;
      if (idx == sel)
        attron(A_REVERSE);
      mvprintw(4 + i, 2, "%s %s", entries[idx].isDir ? "[DIR ]" : "[FILE]",
               entries[idx].name.c_str());
      if (idx == sel)
        attroff(A_REVERSE);
    }
    refresh();

    int ch = getch();
    if (ch == 'q')
      return "";
    if (ch == KEY_UP)
      sel = std::max(sel - 1, 0);
    else if (ch == KEY_DOWN)
      sel = std::min(sel + 1, (int)entries.size() - 1);
    else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      auto slash = dir.find_last_of('/');
      dir = (slash == 0 || slash == std::string::npos) ? "/"
                                                       : dir.substr(0, slash);
      entries = listDir(dir);
      sel = 0;
    } else if (ch == '\n' || ch == KEY_ENTER) {
      if (entries.empty())
        continue;
      const Entry &e = entries[sel];
      std::string full = (dir == "/") ? "/" + e.name : dir + "/" + e.name;
      if (e.isDir) {
        dir = full;
        entries = listDir(dir);
        sel = 0;
      } else if (hasExt(e.name, exts)) {
        return full;
      } else {
        mvprintw(3, 1, "Not a supported file for this technique.");
        refresh();
        getch();
      }
    }
  }
}

// ---- image preview (side-by-side cover vs stego) --------------------------
void showImagePreview(const std::string &coverPath, const std::string &stegoPath) {
  if (!steg::imglsb::previewsEnabled())
    return;
  cv::Mat a = cv::imread(coverPath), b = cv::imread(stegoPath);
  if (a.empty() || b.empty())
    return;
  int h = 480;
  cv::resize(a, a, cv::Size(a.cols * h / a.rows, h));
  cv::resize(b, b, cv::Size(b.cols * h / b.rows, h));
  cv::Mat combined;
  cv::hconcat(a, b, combined);
  cv::putText(combined, "Original", cv::Point(10, 30),
              cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2);
  cv::putText(combined, "Stego", cv::Point(a.cols + 10, 30),
              cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2);
  cv::namedWindow("Cover vs Stego", cv::WINDOW_AUTOSIZE);
  cv::imshow("Cover vs Stego", combined);
  cv::waitKey(1200);
}

void closePreview() {
  if (steg::imglsb::previewsEnabled())
    cv::destroyAllWindows();
}

std::string psnrLine(double v) {
  if (v == std::numeric_limits<double>::infinity())
    return "PSNR: infinite (identical)";
  return "PSNR: " + std::to_string(v) + " dB";
}

// ---- per-technique flows --------------------------------------------------
const std::vector<std::string> kImageExt = {".png", ".jpg", ".jpeg", ".bmp"};
const std::vector<std::string> kBmpExt = {".bmp"};
const std::vector<std::string> kWavExt = {".wav"};
const std::vector<std::string> kVideoExt = {".avi", ".mp4", ".mov", ".mkv"};

void imageLsbEmbed() {
  clear();
  mvprintw(0, 1, "Image LSB — Embed");
  bool textMode = !promptYesNo(2, "Hide an IMAGE inside the cover? (No = hide text)");
  std::string cover = browseForFile("Select COVER image", kImageExt);
  if (cover.empty())
    return;
  try {
    if (textMode) {
      clear();
      std::string msg = promptLine(2, "Message to hide: ");
      std::string key = promptOptionalKey(4);
      std::string out = promptLine(7, "Output image name (e.g. stego.png): ");
      steg::imglsb::embedMessage(cover, out, msg, key);
      showImagePreview(cover, out);
      double p = steg::imglsb::psnrFiles(cover, out);
      closePreview();
      clear();
      pauseMsg("Text embedded -> " + out + "\n" + psnrLine(p));
    } else {
      std::string secret = browseForFile("Select SECRET image", kImageExt);
      if (secret.empty())
        return;
      clear();
      std::string key = promptOptionalKey(2);
      std::string out = promptLine(5, "Output image name (e.g. stego.png): ");
      steg::imglsb::embedImage(cover, secret, out, key);
      showImagePreview(cover, out);
      double p = steg::imglsb::psnrFiles(cover, out);
      closePreview();
      clear();
      pauseMsg("Image embedded -> " + out + "\n" + psnrLine(p));
    }
  } catch (const std::exception &e) {
    closePreview();
    clear();
    pauseMsg(std::string("Failed: ") + e.what());
  }
}

void imageLsbExtract() {
  clear();
  mvprintw(0, 1, "Image LSB — Extract");
  std::string stego = browseForFile("Select STEGO image", kImageExt);
  if (stego.empty())
    return;
  try {
    steg::imglsb::Mode mode = steg::imglsb::detect(stego);
    clear();
    if (mode == steg::imglsb::Mode::Text) {
      std::string key = promptOptionalKey(2);
      std::string msg = steg::imglsb::extractMessage(stego, key);
      clear();
      pauseMsg("Hidden text:\n" + msg);
    } else if (mode == steg::imglsb::Mode::Image) {
      std::string key = promptOptionalKey(2);
      std::string out = promptLine(5, "Save hidden image as (e.g. out.png): ");
      steg::imglsb::extractImage(stego, out, key);
      pauseMsg("Hidden image extracted -> " + out);
    } else {
      pauseMsg("No StegoNinja LSB payload found in this image.");
    }
  } catch (const std::exception &e) {
    clear();
    pauseMsg(std::string("Failed: ") + e.what());
  }
}

void bpcsEmbed() {
  clear();
  mvprintw(0, 1, "Image BPCS — Embed (24-bit BMP cover)");
  std::string cover = browseForFile("Select COVER (.bmp)", kBmpExt);
  if (cover.empty())
    return;
  std::string secret = browseForFile("Select SECRET file (any type)", {});
  if (secret.empty())
    return;
  clear();
  std::string pw = promptLine(2, "Password (blank = none; enables enc+shuffle): ");
  std::string out = promptLine(4, "Output BMP name (e.g. stego.bmp): ");
  try {
    double p = steg::bpcs::embed(cover, secret, out, pw);
    pauseMsg("Embedded -> " + out + "\n" + psnrLine(p));
  } catch (const std::exception &e) {
    pauseMsg(std::string("Failed: ") + e.what());
  }
}

void bpcsExtract() {
  clear();
  mvprintw(0, 1, "Image BPCS — Extract");
  std::string stego = browseForFile("Select STEGO (.bmp)", kBmpExt);
  if (stego.empty())
    return;
  clear();
  std::string pw = promptLine(2, "Password (blank = none): ");
  std::string dir = promptLine(4, "Output directory (e.g. ./out): ");
  try {
    std::string out = steg::bpcs::extract(stego, dir.empty() ? "." : dir, pw);
    pauseMsg("Extracted -> " + out);
  } catch (const std::exception &e) {
    pauseMsg(std::string("Failed: ") + e.what());
  }
}

void audioEmbed() {
  clear();
  mvprintw(0, 1, "Audio LSB — Embed (PCM WAV cover)");
  std::string cover = browseForFile("Select COVER (.wav)", kWavExt);
  if (cover.empty())
    return;
  std::string secret = browseForFile("Select SECRET file (any type)", {});
  if (secret.empty())
    return;
  clear();
  std::string key = promptOptionalKey(2);
  bool rand = promptYesNo(4, "Randomize sample positions?");
  std::string out = promptLine(6, "Output WAV name (e.g. stego.wav): ");
  try {
    double p = steg::audiolsb::embed(cover, secret, out, key, !key.empty(), rand);
    pauseMsg("Embedded -> " + out + "\n" + psnrLine(p));
  } catch (const std::exception &e) {
    pauseMsg(std::string("Failed: ") + e.what());
  }
}

void audioExtract() {
  clear();
  mvprintw(0, 1, "Audio LSB — Extract");
  std::string stego = browseForFile("Select STEGO (.wav)", kWavExt);
  if (stego.empty())
    return;
  clear();
  std::string key = promptOptionalKey(2);
  bool rand = promptYesNo(4, "Was randomize used at embed time?");
  std::string dir = promptLine(6, "Output directory (e.g. ./out): ");
  try {
    std::string out = steg::audiolsb::extract(stego, dir.empty() ? "." : dir,
                                              key, !key.empty(), rand);
    pauseMsg("Extracted -> " + out);
  } catch (const std::exception &e) {
    pauseMsg(std::string("Failed: ") + e.what());
  }
}

void videoEmbed() {
  clear();
  mvprintw(0, 1, "Video LSB — Embed (output is lossless FFV1/AVI)");
  std::string cover = browseForFile("Select COVER video", kVideoExt);
  if (cover.empty())
    return;
  clear();
  std::string msg = promptLine(2, "Message to hide: ");
  steg::videolsb::EmbedOptions o;
  std::string seedStr = promptLine(4, "Seed (0..31): ");
  try {
    o.seed = seedStr.empty() ? 0 : std::stoi(seedStr);
  } catch (...) {
    o.seed = 0;
  }
  o.frameRandom = promptYesNo(6, "Random frame order?");
  o.pixelRandom = promptYesNo(7, "Random pixel order?");
  o.key = promptOptionalKey(9);
  std::string out = promptLine(12, "Output name (e.g. stego.avi): ");
  try {
    steg::videolsb::embed(cover, out, msg, o);
    pauseMsg("Embedded -> " + out);
  } catch (const std::exception &e) {
    pauseMsg(std::string("Failed: ") + e.what());
  }
}

void videoExtract() {
  clear();
  mvprintw(0, 1, "Video LSB — Extract");
  std::string stego = browseForFile("Select STEGO (.avi)", kVideoExt);
  if (stego.empty())
    return;
  clear();
  std::string key = promptLine(2, "Key (blank if not encrypted): ");
  try {
    std::string msg = steg::videolsb::extract(stego, key);
    pauseMsg("Hidden message:\n" + msg);
  } catch (const std::exception &e) {
    pauseMsg(std::string("Failed: ") + e.what());
  }
}

// ---- menu helpers ---------------------------------------------------------
int menu(const std::string &title, const std::vector<std::string> &items) {
  int hi = 0;
  while (true) {
    clear();
    mvprintw(0, 1, "StegoNinja — %s", title.c_str());
    mvprintw(1, 1, "Arrow keys to move, Enter to select.");
    for (int i = 0; i < (int)items.size(); ++i) {
      if (i == hi)
        attron(A_REVERSE);
      mvprintw(3 + i, 2, "%s", items[i].c_str());
      if (i == hi)
        attroff(A_REVERSE);
    }
    refresh();
    int c = getch();
    if (c == KEY_UP)
      hi = (hi == 0) ? items.size() - 1 : hi - 1;
    else if (c == KEY_DOWN)
      hi = (hi + 1) % items.size();
    else if (c == '\n' || c == KEY_ENTER)
      return hi;
  }
}

void techniqueMenu(const std::string &name, void (*embed)(), void (*extract)()) {
  while (true) {
    int a = menu(name, {"Embed", "Extract", "Back"});
    if (a == 0)
      embed();
    else if (a == 1)
      extract();
    else
      return;
  }
}

} // namespace

int main() {
  initscr();
  noecho();
  cbreak();
  keypad(stdscr, TRUE);

  while (true) {
    int t = menu("Main Menu",
                 {"Image LSB", "Image BPCS", "Audio LSB", "Video LSB", "Exit"});
    if (t == 0)
      techniqueMenu("Image LSB", imageLsbEmbed, imageLsbExtract);
    else if (t == 1)
      techniqueMenu("Image BPCS", bpcsEmbed, bpcsExtract);
    else if (t == 2)
      techniqueMenu("Audio LSB", audioEmbed, audioExtract);
    else if (t == 3)
      techniqueMenu("Video LSB", videoEmbed, videoExtract);
    else
      break;
  }

  endwin();
  return 0;
}
