#include "naming.hpp"
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace replay {
std::string replayFilename(const FilenameOptions &options, const FilenameVideo &video, std::time_t requestTime,
                           int64_t seconds)
{
  std::tm local{};
#ifdef _WIN32
  if (localtime_s(&local, &requestTime)) throw std::runtime_error("Cannot format replay request time");
#else
  if (!localtime_r(&requestTime, &local)) throw std::runtime_error("Cannot format replay request time");
#endif
  std::string pattern = options.format.empty() ? "%CCYY-%MM-%DD %hh-%mm-%ss" : options.format;
  std::replace(pattern.begin(), pattern.end(), '\\', '/');
  if (!options.prefix.empty()) {
    std::string prefix = options.prefix;
    if (prefix.back() != ' ') prefix += ' ';
    auto slash = pattern.find_last_of('/');
    pattern.insert(slash == std::string::npos ? 0 : slash + 1, prefix);
  }
  if (!options.suffix.empty()) {
    if (options.suffix.front() != ' ') pattern += ' ';
    pattern += options.suffix;
  }
  // Keep the token ordering of os_generate_formatted_filename. A plain
  // strftime call would interpret OBS's %MM (month) as minutes plus a letter.
  static const char *tokens[][2] = {{"%CCYY", "%Y"}, {"%YY", "%y"}, {"%MM", "%m"}, {"%DD", "%d"}, {"%hh", "%H"},
                                    {"%mm", "%M"},   {"%ss", "%S"}, {"%%", "%%"},  {"%a", "%a"},  {"%A", "%A"},
                                    {"%b", "%b"},    {"%B", "%B"},  {"%d", "%d"},  {"%H", "%H"},  {"%I", "%I"},
                                    {"%m", "%m"},    {"%M", "%M"},  {"%p", "%p"},  {"%S", "%S"},  {"%y", "%y"},
                                    {"%Y", "%Y"},    {"%z", "%z"},  {"%Z", "%Z"}};
  std::string name;
  for (size_t pos = 0; pos < pattern.size();) {
    bool matched = false;
    for (auto &token : tokens) {
      const std::string key = token[0];
      if (pattern.compare(pos, key.size(), key)) continue;
      char value[128]{};
      std::strftime(value, sizeof(value), token[1], &local);
      name += value;
      pos += key.size();
      matched = true;
      break;
    }
    if (matched) continue;
    auto replace = [&](const char *key, const std::string &value) {
      const size_t length = std::char_traits<char>::length(key);
      if (pattern.compare(pos, length, key)) return false;
      name += value;
      pos += length;
      return true;
    };
    std::ostringstream fps;
    if (video.fpsDen <= 1)
      fps << video.fpsNum;
    else
      fps << std::fixed << std::setprecision(2) << double(video.fpsNum) / video.fpsDen;
    if (replace("%FPS", fps.str()) ||
        replace("%CRES", std::to_string(video.baseWidth) + "x" + std::to_string(video.baseHeight)) ||
        replace("%ORES", std::to_string(video.outputWidth) + "x" + std::to_string(video.outputHeight)) ||
        replace("%VF", video.format) || replace("%s", std::to_string(requestTime)))
      continue;
    // OBS drops an unrecognized percent sign and preserves the following text.
    if (pattern[pos] != '%') name += pattern[pos];
    ++pos;
  }
  name += ' ' + std::to_string(seconds) + 's';
  for (char &ch : name) {
    if (ch == '\\') ch = '/';
    if (ch == '*' || ch == '?' || ch == '"' || ch == '|' || ch == ':' || ch == '>' || ch == '<' ||
        static_cast<unsigned char>(ch) < 32 || (!options.allowSpaces && ch == ' '))
      ch = '_';
  }
  // A format may create dated subdirectories, but must stay under the chosen
  // destination. Remove Windows-incompatible trailing spaces and dots too.
  auto path = std::filesystem::u8path(name);
  if (path.has_root_path()) throw std::runtime_error("Replay filename format must be relative");
  std::filesystem::path safe;
  for (const auto &part : path) {
    std::string component = part.u8string();
    if (component == "..") throw std::runtime_error("Replay filename format cannot leave the destination folder");
    if (component == "." || component.empty()) continue;
    while (!component.empty() && (component.back() == ' ' || component.back() == '.')) component.pop_back();
    if (component.empty()) component = "_";
    safe /= std::filesystem::u8path(component);
  }
  return safe.u8string();
}
}
