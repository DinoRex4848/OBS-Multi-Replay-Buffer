#pragma once
#include <cstdint>
namespace replay {
constexpr uint32_t wireMagic = 0x4d525031;
constexpr uint32_t maxPacketSize = 8 * 1024 * 1024;
struct WireHeader {
  uint32_t magic = wireMagic, streams = 0, pathSize = 0;
  uint64_t packets = 0;
};
struct WireStream {
  uint32_t codecSize = 0, nameSize = 0, extraSize = 0;
  int32_t width = 0, height = 0, fpsNum = 0, fpsDen = 1;
  int32_t sampleRate = 0, channels = 0, frameSize = 0, priming = 0;
  int32_t primaries = 1, transfer = 1, colorspace = 1, range = 1, maxLuminance = 0;
};
struct WirePacket {
  int64_t pts = 0, dts = 0, duration = 0;
  uint32_t track = 0, key = 0, size = 0;
};
}
