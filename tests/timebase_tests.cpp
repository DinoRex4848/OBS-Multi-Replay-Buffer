#include "timebase.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace replay;
#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr)) throw std::runtime_error(#expr);                                                                      \
  } while (0)
int main()
{
  try {
    // Expected values match av_rescale_q(value, {num, den}, {1, 1000000}).
    CHECK(toMicroseconds(1, 1, 60) == 16667);
    CHECK(toMicroseconds(1, 1001, 60000) == 16683);
    CHECK(toMicroseconds(2, 1001, 60000) == 33367);
    CHECK(toMicroseconds(1024, 1, 48000) == 21333);
    CHECK(toMicroseconds(1024, 1, 44100) == 23220);
    CHECK(toMicroseconds(-2, 1, 60) == -33333);
    CHECK(toMicroseconds(216000, 1, 60) == 3600000000);
    // Exact halves round away from zero.
    CHECK(toMicroseconds(1, 1, 2000000) == 1);
    CHECK(toMicroseconds(3, 1, 2000000) == 2);
    CHECK(toMicroseconds(-1, 1, 2000000) == -1);
    CHECK(toMicroseconds(0, 1, 60) == 0);
    for (auto [num, den] : {std::pair<int64_t, int64_t>{1, 0}, {1, -60}, {-1, 60}}) {
      bool rejected = false;
      try {
        toMicroseconds(1, num, den);
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      CHECK(rejected);
    }
    std::cout << "PASS: frame/sample durations, NTSC rates, negative offsets, half rounding and invalid timebases\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
