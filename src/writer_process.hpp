#pragma once
#include "history.hpp"
#include <filesystem>

namespace replay {
struct Stream {
  std::string codec, name;
  std::vector<uint8_t> extra;
  int width = 0, height = 0, fpsNum = 0, fpsDen = 1;
  int sampleRate = 0, channels = 0, frameSize = 0, priming = 0;
  int primaries = 1, transfer = 1, colorspace = 1, range = 1, maxLuminance = 0;
};
struct Format {
  std::vector<Stream> streams;
};
struct MuxResult {
  std::filesystem::path path;
  std::string error;
};
void setWriterPath(std::filesystem::path);
// File I/O runs in a disposable child process so the parent can terminate a
// stalled drive without hanging OBS's capture, UI or shutdown threads.
MuxResult writeMkv(const Format &, Selection &, const std::filesystem::path &folder, const std::string &stem,
                   std::function<void()> progress = {}, bool allowSpaces = true);
// Used by the plugin's --multi-replay-writer helper mode / native helper.
int writerMain(int argc, char **argv);
}
