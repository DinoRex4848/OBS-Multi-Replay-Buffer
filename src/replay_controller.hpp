#pragma once
#include "capture.hpp"
#include "hotkeys.hpp"
#include "preferences.hpp"
#include <QObject>
#include <QPointer>
#include <QString>
#include <atomic>
#include <memory>
#include <unordered_map>
#ifdef REPLAY_INTEGRATION_TEST
#include <deque>
#endif

class QAction;
class QTimer;

namespace replay {
class ReplayDock;
class SettingsDialog;

// Coordinates the replay dock, the settings window, hotkeys, the profile's saved
// settings and the capture session. Lives on the OBS UI thread.
class ReplayController : public QObject {
public:
  explicit ReplayController(QWidget *mainWindow);
  ~ReplayController() override;
  QWidget *controls() const;
  bool addSettingsMenu();
  void showSettings();
  // Saves the applied settings, dock presets and hotkey bindings to the current profile.
  bool saveSettings();
  // OBS frontend events.
  void profileChanging();
  void profileChanged();
  void builtInStarted();
  void recordingPaused(bool paused);
  void stopBuffer();
  void shutdown();
#ifdef REPLAY_INTEGRATION_TEST
  void runIntegrationTest(); // Defined in integration_test.cpp.
#endif

private:
#ifdef REPLAY_INTEGRATION_TEST
  friend class IntegrationDriver;
#endif
  // Settings window draft and apply.
  ReplaySettings draftSettings() const;
  void resetDraft();
  void updateSettingsState();
  bool applySettings();
  bool persistSettings(const ReplaySettings &settings);
  void loadSettings();
  // Session.
  void startBuffer();
  void saveReplay(int preset, Time endpoint);
  void refresh();
  void checkRecordingChanges();
  void handleCompleted(const Result &saved);
  void error(const std::string &message, bool saveError = false);
  void onHotkey(HotkeyAction action, Time pressed);
  static void bindingsChanged(void *data, calldata_t *params);

  QPointer<ReplayDock> dock_;
  QPointer<SettingsDialog> dialog_;
  QPointer<QAction> settingsAction_;
  QTimer *timer_ = nullptr;
  Capture capture_;
  std::unique_ptr<Hotkeys> hotkeys_;
  ReplaySettings applied_;
  bool loading_ = false, closed_ = false, profilePending_ = false;
  std::atomic<bool> bindingSavePending_{false};

  // The running session: its destinations and the encoding it started with.
  std::array<std::filesystem::path, presetCount> sessionFolders_{};
  std::string fingerprint_;
  obs_video_info videoInfo_{};
  // Identity of the frontend encoder this session shares; compared, never dereferenced.
  const obs_encoder_t *sharedVideo_ = nullptr;
  bool shareLost_ = false;
  int ticks_ = 0;

  // Dock status: the latest error, warning and save feedback.
  QString error_, warning_, feedback_, feedbackDetail_;
  Time feedbackUntil_ = 0;
  bool saveError_ = false;   // error_ came from a failed save,
  int saveErrorPreset_ = -1; // of this preset (-1: any).
  struct PendingPreset {
    int index;
    QString label;
  };
  std::unordered_map<uint64_t, PendingPreset> pendingPresets_;
  uint64_t lastResultId_ = 0;
#ifdef REPLAY_INTEGRATION_TEST
  std::deque<QString> errors_; // Recent errors, for the UI test report.
  std::vector<Result> testResults_;
#endif
};
} // namespace replay
