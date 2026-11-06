#pragma once
#include "history.hpp"
#include <array>
#include <filesystem>

namespace replay {
// Codecs the writer can mux; the replay output accepts only these.
inline constexpr std::array<const char *, 3> videoCodecs = {"h264", "hevc", "av1"};
inline constexpr std::array<const char *, 6> audioCodecs = {"aac", "flac", "opus", "pcm_s16le", "pcm_s24le", "pcm_f32le"};

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
}
