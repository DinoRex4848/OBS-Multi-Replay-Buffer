#pragma once
#include <cstdint>

namespace replay {
using Time = int64_t; // Monotonic system timeline, microseconds.
constexpr Time microsecondsPerSecond = 1000000;
constexpr Time fromSeconds(int64_t seconds) { return seconds * microsecondsPerSecond; }

// Replay presets shown in the dock. Settings keys and hotkeys are numbered by preset.
constexpr int presetCount = 3;
constexpr int defaultPresetSeconds[presetCount] = {60, 240, 600};
constexpr int minPresetSeconds = 1, maxPresetSeconds = 86400;

// Replay memory budget, in decimal GB as shown in the settings window.
constexpr double defaultBudgetGb = 6;
constexpr double minBudgetGb = 0.05, maxBudgetGb = 256;
constexpr uint64_t bytesPerGb = 1000000000ULL;
constexpr uint64_t defaultBudgetBytes = uint64_t(defaultBudgetGb) * bytesPerGb;

// UI refresh cadence.
constexpr int refreshIntervalMs = 250;
constexpr int encoderCheckEveryTicks = 8; // About every 2 s: recording settings and video format checks.
constexpr Time feedbackDuration = fromSeconds(5);
} // namespace replay
