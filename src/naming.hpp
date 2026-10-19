#pragma once
#include <cstdint>
#include <ctime>
#include <string>

namespace replay {
struct FilenameOptions {
  std::string format = "%CCYY-%MM-%DD %hh-%mm-%ss";
  std::string prefix = "Replay", suffix;
  bool allowSpaces = true;
};
struct FilenameVideo {
  uint32_t fpsNum = 0, fpsDen = 1;
  uint32_t baseWidth = 0, baseHeight = 0, outputWidth = 0, outputHeight = 0;
  std::string format;
};
// OBS 32.1 filename tokens, evaluated at the request's local wall time rather
// than the later writer time. Returns a sanitized, relative path without MKV.
std::string replayFilename(const FilenameOptions &, const FilenameVideo &, std::time_t requestTime, int64_t seconds);
}
