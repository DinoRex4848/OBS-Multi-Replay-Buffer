#pragma once
#include "constants.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

// Text shown in the dock and the Diagnostics tab. Plain functions, so they are
// unit-tested without OBS or Qt.
namespace replay::text {
// Fixed-point number with a '.' separator regardless of the C locale.
std::string fixed(double value, int decimals);
// "2.0 s"
std::string seconds(Time duration);
// "Preset 1 — Replay 1"
std::string presetLabel(int index, const std::string &name);
// Save button tooltip: whether a full, partial or no clip is available.
std::string saveTooltip(Time requested, Time available);
// Diagnostics summary of history, memory and the save queue.
std::string details(Time available, Time target, uint64_t memoryBytes, double budgetGb, size_t queued,
                    const std::string &writing);
// Diagnostics lines naming where this session saves.
std::string destinations(bool separateFolders, const std::array<std::string, presetCount> &names,
                         const std::array<std::filesystem::path, presetCount> &folders);

struct StatusInputs {
  bool paused = false, draining = false, collecting = false, degraded = false;
  // Capture's state message, and whether that message is a capture failure.
  std::string captureMessage;
  bool captureFailed = false;
  std::string error, warning;
  // The latest save feedback; the dock shows it only while feedbackCurrent.
  std::string feedback, feedbackDetail;
  bool feedbackCurrent = false;
};
struct StatusText {
  std::string state;   // One-line dock status.
  std::string tooltip; // Dock status tooltip.
  std::string notice;  // Diagnostics notice; hidden when blank.
};
StatusText status(const StatusInputs &);
} // namespace replay::text
