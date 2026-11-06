#pragma once
#include "preferences.hpp"
#include <QDialog>
#include <array>

class QCheckBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;

namespace replay {
// Diagnostics encoder text before a session starts.
inline constexpr const char *inheritedEncodingText = "Encoding is inherited from OBS recording settings.";

// Tools → Multi-Buffer Settings: a Settings tab edited as a draft (Apply/OK/Cancel)
// and a read-only Diagnostics tab. A view only; ReplayController wires and updates it.
class SettingsDialog : public QDialog {
public:
  explicit SettingsDialog(QWidget *parent);
  ReplaySettings draft() const;
  // Replaces the draft without emitting change signals.
  void setDraft(const ReplaySettings &settings);
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
  std::array<QLabel *, presetCount> presetFolderLabels{};
  std::array<QLineEdit *, presetCount> presetFolders{};
  std::array<QPushButton *, presetCount> presetBrowse{};
  QCheckBox *separateEncoder;
  // Diagnostics tab.
  QLabel *details, *encoder, *notice;
};
} // namespace replay
