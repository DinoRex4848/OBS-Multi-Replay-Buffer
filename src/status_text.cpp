#include "status_text.hpp"
#include <charconv>
#include <cmath>

namespace replay::text {
std::string fixed(double value, int decimals)
{
  // Matches QString::number(value, 'f', decimals), which these texts used before:
  // the exact binary value rounded half away from zero. printf and iostreams round
  // ties to even (1.25 -> "1.2" instead of "1.3") and follow the C locale.
  // 1100 fractional digits expand any double exactly, so nothing rounds early.
  char buffer[1500];
  const auto result =
      std::to_chars(buffer, buffer + sizeof(buffer), std::fabs(value), std::chars_format::fixed, 1100);
  std::string digits(buffer, result.ptr);
  const auto point = digits.find('.');
  bool roundUp = digits[point + 1 + decimals] >= '5';
  digits.resize(decimals ? point + 1 + decimals : point);
  for (auto i = digits.size(); roundUp; --i) {
    if (i == 0) {
      digits.insert(digits.begin(), '1');
      break;
    }
    char &digit = digits[i - 1];
    if (digit == '.') continue;
    roundUp = digit == '9';
    digit = roundUp ? '0' : char(digit + 1);
  }
  return (std::signbit(value) ? "-" : "") + digits;
}
std::string seconds(Time duration) { return fixed(double(duration) / microsecondsPerSecond, 1) + " s"; }
std::string presetLabel(int index, const std::string &name) { return "Preset " + std::to_string(index + 1) + " — " + name; }
std::string saveTooltip(Time requested, Time available)
{
  const char *readiness = available >= requested ? "Full replay available"
                          : available > 0        ? "Partial replay available"
                                                 : "No playable replay available yet";
  return std::string(readiness) + "\nRequested " + seconds(requested) + "; available " + seconds(available);
}
std::string details(Time available, Time target, uint64_t memoryBytes, double budgetGb, size_t queued,
                    const std::string &writing)
{
  return "Available " + seconds(available) + " / target " + seconds(target) + "\nReplay memory " +
         fixed(double(memoryBytes) / 1e9, 3) + " / " + fixed(budgetGb, 2) + " GB · queue " + std::to_string(queued) +
         (writing.empty() ? "" : "\nWriting " + writing);
}
std::string destinations(bool separateFolders, const std::array<std::string, presetCount> &names,
                         const std::array<std::filesystem::path, presetCount> &folders)
{
  if (!separateFolders) return "\nDestination: " + folders[0].u8string();
  std::string text = "\nDestinations:";
  for (int i = 0; i < presetCount; ++i) text += "\n" + presetLabel(i, names[i]) + ": " + folders[i].u8string();
  return text;
}
StatusText status(const StatusInputs &in)
{
  std::string state = in.paused     ? "Paused with recording"
                      : in.draining ? "Draining accepted saves"
                                    : (in.captureMessage.empty() ? "Stopped" : in.captureMessage);
  std::string warning = in.warning;
  if (in.captureFailed) warning = in.captureMessage + (warning.empty() ? "" : "\n" + warning);
  if (in.degraded && in.collecting)
    warning += (warning.empty() ? "" : "\n") + std::string("Memory pressure shortened history; partial clips may be saved.");
  if (!in.error.empty())
    state = "Error: " + in.error;
  else if (!warning.empty())
    state += " · " + warning;
  else if (!in.feedback.empty() && in.feedbackCurrent)
    state = in.feedback;
  StatusText out;
  out.tooltip = in.feedbackDetail.empty() ? state : state + "\n" + in.feedbackDetail;
  out.notice = (in.error.empty() ? "" : "Error: " + in.error + "\n") + warning +
               (in.feedback.empty() ? "" : "\n" + in.feedback + "\n" + in.feedbackDetail);
  out.state = std::move(state);
  return out;
}
} // namespace replay::text
