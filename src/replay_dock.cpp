#include "replay_dock.hpp"
#include "identifiers.hpp"
#include "ui_util.hpp"
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QToolButton>
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

namespace {
// A compact tool button showing an OBS theme icon (the same classes as OBS's own
// Scenes and Sources docks); the text shows instead with themes that have no such icon.
QToolButton *iconButton(QWidget *parent, const QString &text, const char *iconClass)
{
  auto *button = new QToolButton(parent);
  button->setText(text);
  button->setProperty("class", iconClass);
  return button;
}
} // namespace

ReplayDock::ReplayDock(QWidget *parent) : QWidget(parent)
{
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(6);
  start = new QPushButton("Start Buffering", this);
  start->setObjectName(ids::startButton);
  layout->addWidget(start);
  grid_ = new QGridLayout;
  grid_->setHorizontalSpacing(6);
  grid_->setVerticalSpacing(6);
  grid_->setColumnStretch(0, 1);
  grid_->addWidget(new QLabel("Preset"), 0, 0);
  grid_->addWidget(new QLabel("Seconds"), 0, 1);
  // Above the remove column, so adding presets costs no extra dock height.
  add = iconButton(this, "+", "icon-plus");
  grid_->addWidget(add, 0, 3);
  layout->addLayout(grid_);
  connect(add, &QToolButton::clicked, this, [this] {
    if (onAdd) onAdd();
  });
  for (int i = 0; i < defaultPresetCount; ++i) rows.push_back(makeRow(defaultPreset(i)));
  placeRows();
  state = new ElidedLabel(this);
  layout->addWidget(state);
  layout->addStretch();
  settingsShortcut = new QPushButton("Settings", this);
  settingsShortcut->setObjectName(ids::settingsButton);
  settingsShortcut->setToolTip("Open Multi-Buffer Settings");
  layout->addWidget(settingsShortcut, 0, Qt::AlignRight);
}
ReplayDock::Row ReplayDock::makeRow(const Preset &preset)
{
  Row row;
  row.name = new QLineEdit(q(preset.name), this);
  row.name->setMinimumWidth(0);
  row.name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  row.seconds = new QSpinBox(this);
  row.seconds->setRange(minPresetSeconds, maxPresetSeconds);
  row.seconds->setValue(preset.seconds);
  // Wide enough for the longest value plus the arrows; the rest goes to the name.
  row.seconds->setMaximumWidth(row.seconds->fontMetrics().horizontalAdvance(QString::number(maxPresetSeconds)) +
                               3 * row.seconds->fontMetrics().height());
  row.save = new QPushButton("Save", this);
  row.remove = iconButton(this, "×", "icon-trash");
  row.remove->setToolTip("Remove this preset");
  // Connected after the initial values, so building a row is not an edit.
  connect(row.seconds, qOverload<int>(&QSpinBox::valueChanged), this, [this] {
    if (onEdited) onEdited();
  });
  connect(row.name, &QLineEdit::editingFinished, this, [this] {
    if (onEdited) onEdited();
  });
  connect(row.save, &QPushButton::clicked, this, [this, button = row.save] {
    const int index = indexOf(button);
    if (index >= 0 && onSave) onSave(index);
  });
  connect(row.remove, &QToolButton::clicked, this, [this, button = row.remove] {
    const int index = indexOf(button);
    if (index >= 0 && onRemove) onRemove(index);
  });
  return row;
}
// Puts the rows in grid order under the header, after rows were added or removed.
void ReplayDock::placeRows()
{
  for (const auto &row : rows)
    for (QWidget *widget : {static_cast<QWidget *>(row.name), static_cast<QWidget *>(row.seconds),
                            static_cast<QWidget *>(row.save), static_cast<QWidget *>(row.remove)})
      grid_->removeWidget(widget);
  for (size_t i = 0; i < rows.size(); ++i) {
    const int line = int(i) + 1;
    grid_->addWidget(rows[i].name, line, 0);
    grid_->addWidget(rows[i].seconds, line, 1);
    grid_->addWidget(rows[i].save, line, 2);
    grid_->addWidget(rows[i].remove, line, 3);
  }
  add->setToolTip(rows.size() >= size_t(maxPresets) ? "Up to " + QString::number(maxPresets) + " presets"
                                                    : "Add a preset");
}
int ReplayDock::indexOf(const QObject *widget) const
{
  for (size_t i = 0; i < rows.size(); ++i)
    if (rows[i].name == widget || rows[i].seconds == widget || rows[i].save == widget || rows[i].remove == widget)
      return int(i);
  return -1;
}
Presets ReplayDock::presets() const
{
  Presets result;
  for (const auto &row : rows) result.push_back({utf8(row.name->text()), row.seconds->value()});
  return result;
}
void ReplayDock::setPresets(const Presets &presets)
{
  while (rows.size() > presets.size()) removePreset(int(rows.size()) - 1);
  while (rows.size() < presets.size()) addPreset(presets[rows.size()]);
  for (size_t i = 0; i < presets.size(); ++i) {
    rows[i].seconds->setValue(presets[i].seconds);
    rows[i].name->setText(q(presets[i].name));
  }
}
void ReplayDock::addPreset(const Preset &preset)
{
  rows.push_back(makeRow(preset));
  placeRows();
}
void ReplayDock::removePreset(int index)
{
  if (index < 0 || index >= presetCount()) return;
  const auto row = rows[size_t(index)];
  rows.erase(rows.begin() + index);
  for (QWidget *widget : {static_cast<QWidget *>(row.name), static_cast<QWidget *>(row.seconds),
                          static_cast<QWidget *>(row.save), static_cast<QWidget *>(row.remove)}) {
    grid_->removeWidget(widget);
    widget->hide();
    // Deferred: the remove button may be the sender of the click being handled.
    widget->deleteLater();
  }
  placeRows();
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
