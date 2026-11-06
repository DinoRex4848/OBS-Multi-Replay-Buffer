#pragma once
#include "preferences.hpp"
#include <QDialog>
#include <functional>
#include <vector>

class QCheckBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QVBoxLayout;

namespace replay {
// Diagnostics encoder text before a session starts.
inline constexpr const char *inheritedEncodingText = "Encoding is inherited from OBS recording settings.";

// Tools → Multi-Buffer Settings: a Settings tab edited as a draft (Apply/OK/Cancel)
// and a read-only Diagnostics tab. A view only; ReplayController wires and updates it.
class SettingsDialog : public QDialog {
public:
  explicit SettingsDialog(QWidget *parent);
  ReplaySettings draft() const;
  // Replaces the draft without emitting change signals. Also sizes the preset folder
  // rows to settings.presetFolders.
  void setDraft(const ReplaySettings &settings);
  // Adds or removes preset folder rows at the end; new rows start blank.
  void setPresetFolderCount(int count);
  // Enables fields for the current state. `editable`: stopped and idle; `busy`: buffering or draining.
  void updateState(bool editable, bool busy, bool dirty, bool open, const Presets &presets);
  void showError(const QString &message);
  void clearError();

  QTabWidget *tabs;
  QDialogButtonBox *settingsButtons;
  QLabel *settingsLock, *settingsError;
  QDoubleSpinBox *budget;
  QLineEdit *folder;
  QPushButton *browse;
  QCheckBox *separateFolders;
  QWidget *presetFoldersView;
  // One row per preset.
  std::vector<QWidget *> presetFolderRows;
  std::vector<QLabel *> presetFolderLabels;
  std::vector<QLineEdit *> presetFolders;
  std::vector<QPushButton *> presetBrowse;
  // Called when a preset folder field changes.
  std::function<void()> onPresetFolderEdited;
  QCheckBox *separateEncoder;
  // Opens OBS's own Hotkeys settings.
  QPushButton *hotkeysButton;
  // Diagnostics tab.
  QLabel *details, *encoder, *notice;

private:
  QVBoxLayout *presetFoldersLayout_;
};
} // namespace replay
