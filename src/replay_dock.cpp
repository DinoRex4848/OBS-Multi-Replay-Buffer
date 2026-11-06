#include "replay_dock.hpp"
#include "identifiers.hpp"
#include "ui_util.hpp"
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QVBoxLayout>

namespace replay {
ElidedLabel::ElidedLabel(QWidget *parent) : QLabel(parent)
{
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  setMinimumWidth(0);
}
void ElidedLabel::setText(const QString &text)
{
  fullText_ = text;
  setToolTip(text);
  updateText();
}
void ElidedLabel::resizeEvent(QResizeEvent *event)
{
  QLabel::resizeEvent(event);
  updateText();
}
void ElidedLabel::changeEvent(QEvent *event)
{
  QLabel::changeEvent(event);
  updateText();
}
void ElidedLabel::updateText()
{
  QLabel::setText(fontMetrics().elidedText(fullText_.section('\n', 0, 0), Qt::ElideRight, contentsRect().width()));
}

ReplayDock::ReplayDock(QWidget *parent) : QWidget(parent)
{
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(6);
  start = new QPushButton("Start Buffering", this);
  start->setObjectName(ids::startButton);
  layout->addWidget(start);
  auto *grid = new QGridLayout;
  grid->setHorizontalSpacing(6);
  grid->setVerticalSpacing(6);
  grid->setColumnStretch(0, 1);
  grid->addWidget(new QLabel("Preset"), 0, 0);
  grid->addWidget(new QLabel("Seconds"), 0, 1);
  for (int i = 0; i < presetCount; ++i) {
    const auto preset = defaultPreset(i);
    names[i] = new QLineEdit(q(preset.name), this);
    names[i]->setMinimumWidth(0);
    names[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    seconds[i] = new QSpinBox(this);
    seconds[i]->setRange(minPresetSeconds, maxPresetSeconds);
    seconds[i]->setValue(preset.seconds);
    saves[i] = new QPushButton("Save", this);
    grid->addWidget(names[i], i + 1, 0);
    grid->addWidget(seconds[i], i + 1, 1);
    grid->addWidget(saves[i], i + 1, 2);
  }
  layout->addLayout(grid);
  state = new ElidedLabel(this);
  layout->addWidget(state);
  layout->addStretch();
  settingsShortcut = new QPushButton("Settings", this);
  settingsShortcut->setObjectName(ids::settingsButton);
  settingsShortcut->setToolTip("Open Multi-Buffer Settings");
  layout->addWidget(settingsShortcut, 0, Qt::AlignRight);
}
Presets ReplayDock::presets() const
{
  Presets result;
  for (int i = 0; i < presetCount; ++i) result[i] = {utf8(names[i]->text()), seconds[i]->value()};
  return result;
}
void ReplayDock::setPresets(const Presets &presets)
{
  for (int i = 0; i < presetCount; ++i) {
    seconds[i]->setValue(presets[i].seconds);
    names[i]->setText(q(presets[i].name));
  }
}
void ReplayDock::setActive(bool active)
{
  const QString activeClass = active ? "state-active" : "";
  if (start->property("class").toString() == activeClass) return;
  start->setProperty("class", activeClass);
  // Qt only re-evaluates property selectors when the stylesheet changes.
  const auto stylesheet = start->styleSheet();
  start->setStyleSheet("/* refresh active state */");
  start->setStyleSheet(stylesheet);
}
} // namespace replay
