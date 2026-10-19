#include "naming.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace replay;
#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr)) throw std::runtime_error(#expr);                                                                      \
  } while (0)
int main()
{
  try {
#ifdef _WIN32
    _putenv_s("TZ", "UTC");
    _tzset();
#else
    setenv("TZ", "UTC", 1);
    tzset();
#endif
    std::tm request{};
    request.tm_year = 126;
    request.tm_mon = 9;
    request.tm_mday = 12;
    request.tm_hour = 19;
    request.tm_min = 30;
    const auto time = std::mktime(&request);
    FilenameOptions options;
    FilenameVideo video{60000, 1001, 3840, 2160, 1920, 1080, "NV12"};
    CHECK(replayFilename(options, video, time, 60) == "Replay 2026-10-12 19-30-00 60s");
    CHECK(replayFilename(options, video, time, 240) == "Replay 2026-10-12 19-30-00 240s");
    // The formatter is given the original request clock, independent of when
    // it is called or when the queued writer eventually runs.
    CHECK(replayFilename(options, video, time - 2, 60) == "Replay 2026-10-12 19-29-58 60s");
    options.allowSpaces = false;
    CHECK(replayFilename(options, video, time, 600) == "Replay_2026-10-12_19-30-00_600s");
    options.format = "%CCYY/%MM/%DD/%hh-%mm-%ss";
    options.prefix = "Clip";
    options.suffix = "game";
    // The formatter returns native filesystem paths, including Windows separators.
    CHECK(replayFilename(options, video, time, 60) ==
          std::filesystem::u8path("2026/10/12/Clip_19-30-00_game_60s").make_preferred().u8string());
    options.allowSpaces = true;
    options.format = "%Y-%m-%d %H-%M-%S %YY %% %UNKNOWN";
    options.prefix.clear();
    options.suffix.clear();
    CHECK(replayFilename(options, video, time, 7) == "2026-10-12 19-30-00 26 % UNKNOWN 7s");
    options.format = "%FPS %CRES %ORES %VF %s";
    CHECK(replayFilename(options, video, time, 1) == "59.94 3840x2160 1920x1080 NV12 " + std::to_string(time) + " 1s");
    options.format = "dir\\%CCYY_%hh:%mm?*\"|<>";
    CHECK(replayFilename(options, video, time, 60) ==
          std::filesystem::u8path("dir/2026_19_30______ 60s").make_preferred().u8string());
    options.format = "%CCYY/../clip";
    bool rejected = false;
    try {
      replayFilename(options, video, time, 60);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    CHECK(rejected);
    options.format = "/clip";
    rejected = false;
    try {
      replayFilename(options, video, time, 60);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    CHECK(rejected);
    std::cout
        << "PASS: OBS tokens, original request clock, durations, spaces, prefix/suffix, subfolders and sanitization\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
