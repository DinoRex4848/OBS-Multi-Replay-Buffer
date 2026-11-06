#pragma once
#include <QString>
#include <filesystem>
#include <string>

// Conversions between Qt text and the UTF-8 strings and paths used below the UI.
namespace replay {
inline QString q(const std::string &text) { return QString::fromUtf8(text.c_str()); }
inline QString pathText(const std::filesystem::path &path) { return q(path.u8string()); }
inline std::string utf8(const QString &text) { return text.toUtf8().toStdString(); }
inline std::filesystem::path filePath(const QString &text) { return std::filesystem::u8path(text.toUtf8().constData()); }
} // namespace replay
