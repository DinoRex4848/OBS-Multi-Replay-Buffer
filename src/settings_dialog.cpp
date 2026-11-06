#include "settings_dialog.hpp"
#include "identifiers.hpp"
#include "ui_util.hpp"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

namespace replay {
namespace {
const char *const recordingFolderHint = "Use OBS recording folder";

QLabel *selectableLabel(QWidget *parent, const QString &text = {})
{
  auto *label = new QLabel(text, parent);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return label;
}
// A folder field with a Browse… button that picks an existing directory.
QWidget *folderRow(QWidget *parent, QLineEdit *&field, QPushButton *&browse, QWidget *dialog, QString title)
{
  auto *row = new QWidget(parent);
  auto *layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  field = new QLineEdit(row);
  field->setMinimumWidth(0);
  field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  field->setPlaceholderText(recordingFolderHint);
  browse = new QPushButton("Browse…", row);
  layout->addWidget(field, 1);
  layout->addWidget(browse);
  QObject::connect(browse, &QPushButton::clicked, row, [dialog, field, title] {
    const auto value = QFileDialog::getExistingDirectory(dialog, title, field->text());
    if (!value.isEmpty()) field->setText(value);
  });
  return row;
}
} // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
  setObjectName(ids::settingsDialog);
  setWindowTitle("Multi-Buffer Settings");
  resize(600, 480);
  auto *windowLayout = new QVBoxLayout(this);
  tabs = new QTabWidget(this);
  windowLayout->addWidget(tabs);
  auto addPage = [this](const char *title) {
    auto *scroll = new QScrollArea(tabs);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget(scroll);
    scroll->setWidget(page);
    tabs->addTab(scroll, title);
    return page;
  };
  auto *settingsPage = addPage("Settings");
  auto *diagnosticsPage = addPage("Diagnostics");
  settingsLock = new QLabel("Stop buffering and wait for accepted saves to finish before changing settings.", this);
  settingsLock->setWordWrap(true);
  windowLayout->addWidget(settingsLock);
  settingsError = selectableLabel(this);
  settingsError->hide();
  windowLayout->addWidget(settingsError);
  settingsButtons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
  windowLayout->addWidget(settingsButtons);

  auto *settingsLayout = new QVBoxLayout(settingsPage);
  settingsLayout->setContentsMargins(8, 8, 8, 8);
  settingsLayout->setSpacing(6);
  budget = new QDoubleSpinBox(settingsPage);
  budget->setRange(minBudgetGb, maxBudgetGb);
  budget->setDecimals(2);
  budget->setValue(defaultBudgetGb);
  budget->setSuffix(" GB");
  budget->setToolTip("Decimal GB of replay memory; excludes OBS and encoder allocations. Change while stopped.");
  settingsLayout->addWidget(new QLabel("Replay RAM budget", settingsPage));
  settingsLayout->addWidget(budget);
  settingsLayout->addWidget(new QLabel("Save folder", settingsPage));
  settingsLayout->addWidget(folderRow(settingsPage, folder, browse, this, "Replay destination"));
  separateFolders = new QCheckBox("Use separate folders for each preset", settingsPage);
  separateFolders->setToolTip("Each blank preset folder uses OBS's recording folder. The shared save folder is used "
                              "only when separate folders are turned off.");
  settingsLayout->addWidget(separateFolders);
  presetFoldersView = new QWidget(settingsPage);
  presetFoldersLayout_ = new QVBoxLayout(presetFoldersView);
  presetFoldersLayout_->setContentsMargins(0, 0, 0, 0);
  auto *folderHint = new QLabel("Leave a preset folder blank to use OBS's recording folder.", presetFoldersView);
  folderHint->setWordWrap(true);
  presetFoldersLayout_->addWidget(folderHint);
  setPresetFolderCount(defaultPresetCount);
  settingsLayout->addWidget(presetFoldersView);
  separateEncoder = new QCheckBox("Use separate replay encoder", settingsPage);
  separateEncoder->setToolTip(
      "Leave unchecked to share OBS's recording encoders (default). Check to use separate replay encoders;\n"
      "recording and replay then run independently and require two video encodes when both are active.\n"
      "When sharing, pausing the recording also pauses replay collection (paused time is cut from replays),\n"
      "and whichever starts second begins at the encoder's next keyframe.");
  settingsLayout->addWidget(separateEncoder);

  auto *diagnosticsLayout = new QVBoxLayout(diagnosticsPage);
  diagnosticsLayout->setContentsMargins(8, 8, 8, 8);
  details = selectableLabel(diagnosticsPage);
  diagnosticsLayout->addWidget(details);
  encoder = selectableLabel(diagnosticsPage, inheritedEncodingText);
  diagnosticsLayout->addWidget(encoder);
  notice = selectableLabel(diagnosticsPage);
  diagnosticsLayout->addWidget(notice);
  diagnosticsLayout->addStretch();
  settingsLayout->addStretch();
}
void SettingsDialog::setPresetFolderCount(int count)
{
  while (int(presetFolders.size()) > count) {
    delete presetFolderRows.back();
    presetFolderRows.pop_back();
    presetFolderLabels.pop_back();
    presetFolders.pop_back();
    presetBrowse.pop_back();
  }
  while (int(presetFolders.size()) < count) {
    const int index = int(presetFolders.size());
    auto *row = new QWidget(presetFoldersView);
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    auto *label = new QLabel(row);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    rowLayout->addWidget(label);
    QLineEdit *field = nullptr;
    QPushButton *button = nullptr;
    rowLayout->addWidget(
        folderRow(row, field, button, this, "Replay preset " + QString::number(index + 1) + " destination"));
    connect(field, &QLineEdit::textChanged, this, [this] {
      if (onPresetFolderEdited) onPresetFolderEdited();
    });
    presetFoldersLayout_->addWidget(row);
    presetFolderRows.push_back(row);
    presetFolderLabels.push_back(label);
    presetFolders.push_back(field);
    presetBrowse.push_back(button);
  }
}
ReplaySettings SettingsDialog::draft() const
{
  ReplaySettings settings;
  settings.budgetGb = budget->value();
  settings.folder = utf8(folder->text());
  settings.shareEncoder = !separateEncoder->isChecked();
  settings.separateFolders = separateFolders->isChecked();
  settings.presetFolders.clear();
  for (const auto *field : presetFolders) settings.presetFolders.push_back(utf8(field->text()));
  return settings;
}
void SettingsDialog::setDraft(const ReplaySettings &settings)
{
  const QSignalBlocker budgetBlock(budget), folderBlock(folder), shareBlock(separateEncoder),
      separateBlock(separateFolders);
  budget->setValue(settings.budgetGb);
  folder->setText(q(settings.folder));
  separateEncoder->setChecked(!settings.shareEncoder);
  separateFolders->setChecked(settings.separateFolders);
  setPresetFolderCount(int(settings.presetFolders.size()));
  for (size_t i = 0; i < presetFolders.size(); ++i) {
    const QSignalBlocker block(presetFolders[i]);
    presetFolders[i]->setText(q(settings.presetFolders[i]));
  }
}
void SettingsDialog::updateState(bool editable, bool busy, bool dirty, bool open, const Presets &presets)
{
  budget->setEnabled(editable);
  const bool separate = separateFolders->isChecked();
  folder->setEnabled(editable && !separate);
  browse->setEnabled(editable && !separate);
  separateFolders->setEnabled(editable);
  presetFoldersView->setVisible(separate);
  setPresetFolderCount(int(presets.size()));
  for (size_t i = 0; i < presetFolders.size(); ++i) {
    presetFolders[i]->setEnabled(editable && separate);
    presetBrowse[i]->setEnabled(editable && separate);
    presetFolderLabels[i]->setText(
        QString("Preset %1 — %2 (%3 s)").arg(int(i) + 1).arg(q(presets[i].name)).arg(presets[i].seconds));
  }
  separateEncoder->setEnabled(editable);
  settingsLock->setVisible(busy);
  settingsButtons->button(QDialogButtonBox::Apply)->setEnabled(editable && dirty);
  settingsButtons->button(QDialogButtonBox::Ok)->setEnabled(open && (!dirty || editable));
}
void SettingsDialog::showError(const QString &message)
{
  settingsError->setText(message);
  settingsError->show();
}
void SettingsDialog::clearError()
{
  settingsError->clear();
  settingsError->hide();
}
} // namespace replay
