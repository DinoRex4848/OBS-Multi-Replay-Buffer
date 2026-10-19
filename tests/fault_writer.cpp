#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
int main()
{
  const char *mode = std::getenv("REPLAY_TEST_FAULT");
  if (mode && std::string(mode) == "stall") std::this_thread::sleep_for(std::chrono::seconds(30));
  std::fputs("Simulated disk full: no space left on device\n", stderr);
  return 28;
}
