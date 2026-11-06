#pragma once
#include "preferences.hpp"
#include <QLabel>
#include <QWidget>
#include <functional>
#include <vector>

class QGridLayout;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;

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

// The "Multi-Length Replay" dock: Start/Stop, one row per preset with Save and
// remove (×), an add (+) button in the header, status and a Settings shortcut. A view only;
// ReplayController wires and updates it.
class ReplayDock : public QWidget {
public:
  struct Row {
    QLineEdit *name;
    QSpinBox *seconds;
    QPushButton *save;
    QToolButton *remove;
  };
  explicit ReplayDock(QWidget *parent);
  Presets presets() const;
  int presetCount() const { return int(rows.size()); }
  // Rebuilds the rows to match `presets`.
  void setPresets(const Presets &presets);
  void addPreset(const Preset &preset);
  void removePreset(int index);
  // Re-applies the stylesheet so a theme's [class="state-active"] rule takes effect.
  void setActive(bool active);

  // Set by ReplayController. `index` is the row's position when it is clicked.
  std::function<void(int index)> onSave, onRemove;
  std::function<void()> onAdd, onEdited;

  QPushButton *start;
  std::vector<Row> rows;
  QToolButton *add;
  ElidedLabel *state;
  QPushButton *settingsShortcut;

private:
  Row makeRow(const Preset &preset);
  void placeRows();
  int indexOf(const QObject *widget) const;
  QGridLayout *grid_;
};
} // namespace replay
