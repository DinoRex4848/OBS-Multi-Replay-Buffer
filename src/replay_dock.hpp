#pragma once
#include "preferences.hpp"
#include <QLabel>
#include <QWidget>
#include <array>

class QLineEdit;
class QPushButton;
class QSpinBox;

namespace replay {
// A label that elides its first line, so a long error or filename never widens the dock.
class ElidedLabel : public QLabel {
public:
  explicit ElidedLabel(QWidget *parent);
  // Hides QLabel::setText; also sets the tooltip to the full text.
  void setText(const QString &text);
  const QString &fullText() const { return fullText_; }

protected:
  void resizeEvent(QResizeEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  void updateText();
  QString fullText_;
};

// The "Multi-Length Replay" dock: Start/Stop, one row per preset, status and a
// Settings shortcut. A view only; ReplayController wires and updates it.
class ReplayDock : public QWidget {
public:
  explicit ReplayDock(QWidget *parent);
  Presets presets() const;
  void setPresets(const Presets &presets);
  // Re-applies the stylesheet so a theme's [class="state-active"] rule takes effect.
  void setActive(bool active);

  QPushButton *start;
  std::array<QLineEdit *, presetCount> names{};
  std::array<QSpinBox *, presetCount> seconds{};
  std::array<QPushButton *, presetCount> saves{};
  ElidedLabel *state;
  QPushButton *settingsShortcut;
};
} // namespace replay
