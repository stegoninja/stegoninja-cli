// stegoninja — unified command-line front-end.
//
// One binary, one steganography engine, four techniques. Fully scriptable:
// every path is non-interactive, prints machine-friendly output, and exits
// non-zero on any error. See `stegoninja --help`.
#include "engine/engine.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const char *kVersion = "stegoninja 2.0.0 (StegoNinja unified CLI)";

// ---- tiny flag parser -----------------------------------------------------
struct Args {
  std::map<std::string, std::string> values; // --name value
  std::vector<std::string> flags;            // --name (boolean)

  bool has(const std::string &k) const {
    return values.count(k) ||
           std::find(flags.begin(), flags.end(), k) != flags.end();
  }
  bool boolFlag(const std::string &k) const {
    return std::find(flags.begin(), flags.end(), k) != flags.end();
  }
  std::string get(const std::string &k, const std::string &dflt = "") const {
    auto it = values.find(k);
    return it == values.end() ? dflt : it->second;
  }
  std::string require(const std::string &k) const {
    auto it = values.find(k);
    if (it == values.end())
      throw std::invalid_argument("missing required option " + k);
    return it->second;
  }
};

// Options that always take a value; everything else here is a boolean flag.
bool takesValue(const std::string &name) {
  static const std::vector<std::string> valued = {
      "--cover",     "--secret",  "--stego",      "--output",
      "--message",   "--message-file", "--password", "--key",
      "--seed",      "--frame-mode",   "--pixel-mode"};
  return std::find(valued.begin(), valued.end(), name) != valued.end();
}

Args parse(int argc, char **argv, int start) {
  Args a;
  for (int i = start; i < argc; ++i) {
    std::string tok = argv[i];
    if (tok.rfind("--", 0) != 0)
      throw std::invalid_argument("unexpected argument: " + tok);
    if (tok == "--encrypt" || tok == "--randomize") {
      a.flags.push_back(tok);
      continue;
    }
    if (!takesValue(tok))
      throw std::invalid_argument("unknown option: " + tok);
    if (i + 1 >= argc)
      throw std::invalid_argument("missing value for " + tok);
    std::string key = (tok == "--key") ? "--password" : tok;
    a.values[key] = argv[++i];
  }
  return a;
}

std::string password(const Args &a) {
  return a.get("--password");
}

// Encryption is ON when --encrypt is given; it then requires a password. BPCS
// is the exception: a non-empty --password alone enables its coupled
// obfuscation+shuffle (documented in its help).
bool encryptOn(const Args &a) { return a.boolFlag("--encrypt"); }
void requirePasswordIfEncrypt(const Args &a) {
  if (encryptOn(a) && password(a).empty())
    throw std::invalid_argument("--encrypt requires a non-empty --password");
}

void printPsnr(double v) {
  if (v == std::numeric_limits<double>::infinity())
    std::cout << "PSNR: infinite (identical)\n";
  else
    std::cout << "PSNR: " << v << " dB\n";
}

// ---- help -----------------------------------------------------------------
void topHelp() {
  std::cout <<
      "StegoNinja — offline steganography (unified CLI)\n\n"
      "Usage: stegoninja <technique> <embed|extract> [options]\n\n"
      "Techniques (all support embed and extract):\n"
      "  image-lsb   Hide text OR an image inside a cover image (LSB)\n"
      "  bpcs        Hide a file inside a 24-bit BMP (BPCS)\n"
      "  audio       Hide a file inside a PCM WAV (LSB)\n"
      "  video       Hide a text message inside a video (FFV1/AVI LSB)\n\n"
      "Global:\n"
      "  -h, --help        Show this help (or `<technique> --help`)\n"
      "  --version         Print version\n\n"
      "Common options (as applicable per technique):\n"
      "  --cover P  --secret P  --stego P  --output P\n"
      "  --message T  --message-file P  --password K (--key alias)\n"
      "  --encrypt  --randomize  --seed N  --frame-mode seq|rand  --pixel-mode seq|rand\n\n"
      "Note: --encrypt uses a Vigenere byte-cipher — obfuscation, NOT strong\n"
      "encryption. Extraction needs the SAME password/flags/seed used to embed.\n";
}

void techHelp(const std::string &t) {
  if (t == "image-lsb")
    std::cout <<
        "stegoninja image-lsb embed   --cover <img> (--message <t> | --message-file <f> | --secret <img>)\n"
        "                             --output <img> [--encrypt --password <k>]\n"
        "stegoninja image-lsb extract --stego <img> [--output <file>] [--encrypt --password <k>]\n\n"
        "Text and image-in-image are distinct, magic-guarded formats; extract\n"
        "auto-detects which one a stego image carries.\n";
  else if (t == "bpcs")
    std::cout <<
        "stegoninja bpcs embed   --cover <bmp> --secret <file> --output <bmp> [--password <k>]\n"
        "stegoninja bpcs extract --stego <bmp> --output <dir>  [--password <k>]\n\n"
        "Cover must be a 24-bit BMP. A non-empty --password enables BOTH Vigenere\n"
        "obfuscation and a password-seeded block shuffle (use the same to extract).\n";
  else if (t == "audio")
    std::cout <<
        "stegoninja audio embed   --cover <wav> --secret <file> --output <wav> [--encrypt --randomize --password <k>]\n"
        "stegoninja audio extract --stego <wav> [--output <dir>] [--encrypt --randomize --password <k>]\n\n"
        "Cover must be a PCM WAV. --encrypt obfuscates; --randomize shuffles sample\n"
        "positions. Extract must use the SAME password and flags.\n";
  else if (t == "video")
    std::cout <<
        "stegoninja video embed   --cover <vid> (--message <t> | --message-file <f>) --output <out.avi>\n"
        "                         [--seed N] [--frame-mode seq|rand] [--pixel-mode seq|rand] [--encrypt --password <k>]\n"
        "stegoninja video extract --stego <in.avi> [--output <file>] [--password <k>]\n\n"
        "--seed is 0..31 (stored in-band). Output is lossless FFV1/AVI. Encryption\n"
        "is auto-detected on extract; supply --password if the stego is encrypted.\n";
  else
    topHelp();
}

int frameMode(const Args &a, const std::string &opt) {
  std::string v = a.get(opt, "seq");
  return (v == "rand" || v == "random" || v == "2") ? 2 : 1;
}

// ---- technique dispatch ---------------------------------------------------
int runImageLsb(const std::string &action, const Args &a) {
  requirePasswordIfEncrypt(a);
  std::string key = encryptOn(a) ? password(a) : "";
  if (action == "embed") {
    std::string cover = a.require("--cover");
    std::string output = a.require("--output");
    bool hasSecret = a.has("--secret");
    bool hasMsg = a.has("--message") || a.has("--message-file");
    if (hasSecret == hasMsg)
      throw std::invalid_argument(
          "image-lsb embed needs exactly one of --message/--message-file or --secret");
    if (hasSecret) {
      steg::imglsb::embedImage(cover, a.require("--secret"), output, key);
    } else {
      std::string msg = a.has("--message-file")
                            ? [&] {
                                std::ifstream f(a.get("--message-file"));
                                if (!f)
                                  throw std::runtime_error(
                                      "cannot open --message-file");
                                std::stringstream ss;
                                ss << f.rdbuf();
                                std::string s = ss.str();
                                if (!s.empty() && s.back() == '\n')
                                  s.pop_back();
                                return s;
                              }()
                            : a.get("--message");
      steg::imglsb::embedMessage(cover, output, msg, key);
    }
    printPsnr(steg::imglsb::psnrFiles(cover, output));
    std::cout << "Embedded -> " << output << "\n";
    return 0;
  }
  // extract
  std::string stego = a.require("--stego");
  steg::imglsb::Mode mode = steg::imglsb::detect(stego);
  if (mode == steg::imglsb::Mode::Text) {
    std::string msg = steg::imglsb::extractMessage(stego, key);
    if (a.has("--output")) {
      std::ofstream out(a.get("--output"), std::ios::binary);
      out << msg;
    }
    std::cout << msg << "\n";
    return 0;
  }
  if (mode == steg::imglsb::Mode::Image) {
    std::string output = a.require("--output");
    steg::imglsb::extractImage(stego, output, key);
    std::cout << "Extracted image -> " << output << "\n";
    return 0;
  }
  throw std::runtime_error("No StegoNinja LSB payload found in " + stego);
}

int runBpcs(const std::string &action, const Args &a) {
  if (action == "embed") {
    double p = steg::bpcs::embed(a.require("--cover"), a.require("--secret"),
                                 a.require("--output"), password(a));
    printPsnr(p);
    std::cout << "Embedded -> " << a.get("--output") << "\n";
  } else {
    std::string out = steg::bpcs::extract(a.require("--stego"),
                                          a.require("--output"), password(a));
    std::cout << "Extracted -> " << out << "\n";
  }
  return 0;
}

int runAudio(const std::string &action, const Args &a) {
  requirePasswordIfEncrypt(a);
  bool encrypt = encryptOn(a);
  bool randomize = a.boolFlag("--randomize");
  if (action == "embed") {
    double p = steg::audiolsb::embed(a.require("--cover"), a.require("--secret"),
                                     a.require("--output"), password(a),
                                     encrypt, randomize);
    printPsnr(p);
    std::cout << "Embedded -> " << a.get("--output") << "\n";
  } else {
    std::string out = steg::audiolsb::extract(a.require("--stego"),
                                              a.get("--output", "."),
                                              password(a), encrypt, randomize);
    std::cout << "Extracted -> " << out << "\n";
  }
  return 0;
}

int runVideo(const std::string &action, const Args &a) {
  requirePasswordIfEncrypt(a);
  if (action == "embed") {
    std::string msg = a.get("--message");
    if (a.has("--message-file")) {
      std::ifstream f(a.get("--message-file"));
      if (!f)
        throw std::runtime_error("cannot open --message-file");
      std::stringstream ss;
      ss << f.rdbuf();
      msg = ss.str();
      if (!msg.empty() && msg.back() == '\n')
        msg.pop_back();
    }
    steg::videolsb::EmbedOptions o;
    o.seed = a.has("--seed") ? std::stoi(a.get("--seed")) : 0;
    o.frameRandom = frameMode(a, "--frame-mode") == 2;
    o.pixelRandom = frameMode(a, "--pixel-mode") == 2;
    o.key = encryptOn(a) ? password(a) : "";
    std::string cover = a.require("--cover");
    std::string output = a.require("--output");
    steg::videolsb::embed(cover, output, msg, o);
    std::cout << "Embedded -> " << output << "\n";
  } else {
    std::string msg = steg::videolsb::extract(a.require("--stego"), password(a));
    if (a.has("--output")) {
      std::ofstream out(a.get("--output"), std::ios::binary);
      out << msg;
    }
    std::cout << msg << "\n";
  }
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    topHelp();
    return 2;
  }
  std::string first = argv[1];
  if (first == "-h" || first == "--help") {
    topHelp();
    return 0;
  }
  if (first == "--version") {
    std::cout << kVersion << "\n";
    return 0;
  }

  const std::string &technique = first;
  if (technique != "image-lsb" && technique != "bpcs" && technique != "audio" &&
      technique != "video") {
    std::cerr << "Unknown technique: " << technique << "\n\n";
    topHelp();
    return 2;
  }

  if (argc < 3 || std::string(argv[2]) == "-h" ||
      std::string(argv[2]) == "--help") {
    techHelp(technique);
    return argc < 3 ? 2 : 0;
  }

  std::string action = argv[2];
  if (action != "embed" && action != "extract") {
    std::cerr << "Unknown action '" << action << "' for " << technique
              << " (expected embed|extract)\n";
    return 2;
  }

  try {
    Args a = parse(argc, argv, 3);
    if (technique == "image-lsb")
      return runImageLsb(action, a);
    if (technique == "bpcs")
      return runBpcs(action, a);
    if (technique == "audio")
      return runAudio(action, a);
    return runVideo(action, a);
  } catch (const std::invalid_argument &e) {
    std::cerr << "Usage error: " << e.what() << "\n";
    return 2;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
