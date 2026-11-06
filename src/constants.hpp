#pragma once
#include <cstdint>

namespace replay {
using Time = int64_t; // Monotonic system timeline, microseconds.
constexpr Time microsecondsPerSecond = 1000000;
constexpr Time fromSeconds(int64_t seconds) { return seconds * microsecondsPerSecond; }

// Streams in a replay: one video track plus up to six OBS audio tracks.
constexpr int maxAudioTracks = 6;
constexpr int maxStreams = 1 + maxAudioTracks;

// Replay presets shown in the dock. Settings keys and hotkeys are numbered by preset.
constexpr int minPresets = 1, maxPresets = 8;
constexpr int defaultPresetCount = 2; // A profile with no saved replay settings.
constexpr int legacyPresetCount = 3;  // Saved before the count was configurable.
constexpr int newPresetSeconds = 60;  // A preset added with the + button.
constexpr int minPresetSeconds = 1, maxPresetSeconds = 86400;
// Length a saved slot falls back to when its key is missing; the first three are
// the defaults of releases that always had three presets.
constexpr int slotDefaultSeconds(int index)
{
  constexpr int legacy[legacyPresetCount] = {60, 240, 600};
  return index >= 0 && index < legacyPresetCount ? legacy[index] : newPresetSeconds;
}

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
