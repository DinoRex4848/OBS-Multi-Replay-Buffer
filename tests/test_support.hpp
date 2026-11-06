#pragma once
// Minimal helpers shared by the C++ test programs. Each test is a plain
// executable that returns 0 on success, so CTest needs no framework.
#include <cstdlib>
#include <stdexcept>
#include <string>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr)) throw std::runtime_error(#expr);                                                                      \
  } while (0)

inline void setEnvironment(const char *name, const char *value)
{
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}
