#pragma once
#include <cstdint>
#include <stdexcept>

namespace replay {
// value * num / den seconds in microseconds, rounded half away from zero like
// av_rescale_q. The OBS module must not import FFmpeg: OBS releases ship
// different FFmpeg majors (32.1 has avutil-59, 32.2 has avutil-60).
inline int64_t toMicroseconds(int64_t value, int64_t num, int64_t den)
{
  if (num < 0 || den <= 0) throw std::runtime_error("Invalid encoder timebase");
  const int64_t whole = value / den, rest = value % den;
  // |rest| < den, so this stays small for OBS frame rates and sample rates.
  const int64_t scaled = rest * num * 1000000;
  return whole * num * 1000000 + (scaled + (scaled < 0 ? -den / 2 : den / 2)) / den;
}
} // namespace replay
