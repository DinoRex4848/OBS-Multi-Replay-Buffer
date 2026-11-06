#include "replay_controller.hpp"
#include "identifiers.hpp"
#include "replay_dock.hpp"
#include "recording_encoders.hpp"
#include "settings_dialog.hpp"
#include "status_text.hpp"
#include "ui_util.hpp"
#include <QAction>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <algorithm>

namespace replay {
namespace {
const char *const persistFailed = "Could not persist replay settings to the current OBS profile";
const char *const shareLostText = "OBS recreated its recording encoder. A recording started now would use a second "
                                  "encoder; restart buffering to share again.";
bool sameVideoFormat(const obs_video_info &a, const obs_video_info &b)
{
  return a.output_width == b.output_width && a.output_height == b.output_height && a.output_format == b.output_format &&
         a.colorspace == b.colorspace && a.range == b.range && a.fps_num == b.fps_num && a.fps_den == b.fps_den;
}
} // namespace

ReplayController::ReplayController(QWidget *mainWindow) : QObject(mainWindow)
{
  dock_ = new ReplayDock(mainWindow);
  dialog_ = new SettingsDialog(mainWindow);
  connect(dialog_->settingsButtons, &QDialogButtonBox::accepted, this, [this] {
    if (applySettings()) dialog_->accept();
  });
  connect(dialog_->settingsButtons, &QDialogButtonBox::rejected, dialog_, &QDialog::reject);
  connect(dialog_->settingsButtons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
          [this] { applySettings(); });
  connect(dialog_, &QDialog::finished, this, [this] { resetDraft(); });
  connect(dock_->start, &QPushButton::clicked, this, [this] {
    if (capture_.status().collecting)
      stopBuffer();
    else
      startBuffer();
  });
  // Preset edits save immediately; settings window edits wait for Apply/OK.
  dock_->onSave = [this](int preset) { saveReplay(preset, clockNow()); };
  dock_->onAdd = [this] { addPreset(); };
  dock_->onRemove = [this](int preset) { removePreset(preset); };
  dock_->onEdited = [this] { saveSettings(); };
  dialog_->onPresetFolderEdited = [this] { updateSettingsState(); };
  connect(dock_->settingsShortcut, &QPushButton::clicked, this, [this] { showSettings(); });
  hotkeys_ = std::make_unique<Hotkeys>([this](HotkeyAction action, Time pressed) { onHotkey(action, pressed); });
  loadSettings();
  connect(dialog_->budget, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { updateSettingsState(); });
  connect(dialog_->folder, &QLineEdit::textChanged, this, [this] { updateSettingsState(); });
  connect(dialog_->separateEncoder, &QCheckBox::toggled, this, [this] { updateSettingsState(); });
  connect(dialog_->separateFolders, &QCheckBox::toggled, this, [this] { updateSettingsState(); });
  signal_handler_connect(obs_get_signal_handler(), "hotkey_bindings_changed", bindingsChanged, this);
  timer_ = new QTimer(this);
  timer_->setInterval(refreshIntervalMs);
  connect(timer_, &QTimer::timeout, this, [this] { refresh(); });
  timer_->start();
  refresh();
}
ReplayController::~ReplayController()
{
  shutdown();
  delete settingsAction_.data();
  hotkeys_.reset();
  delete dock_.data();
  delete dialog_.data();
}
QWidget *ReplayController::controls() const { return dock_; }
bool ReplayController::addSettingsMenu()
{
  settingsAction_ = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("Multi-Buffer Settings"));
  if (!settingsAction_) return false;
  settingsAction_->setObjectName(ids::settingsAction);
  connect(settingsAction_, &QAction::triggered, this, [this] { showSettings(); });
  return true;
}
void ReplayController::showSettings()
{
  if (closed_ || profilePending_) return;
  if (!dialog_->isVisible()) {
    resetDraft();
    dialog_->tabs->setCurrentIndex(0);
  }
  refresh();
  dialog_->show();
  dialog_->raise();
  dialog_->activateWindow();
}

ReplaySettings ReplayController::draftSettings() const { return dialog_->draft(); }
void ReplayController::resetDraft()
{
  dialog_->setDraft(applied_);
  dialog_->clearError();
  updateSettingsState();
}
void ReplayController::updateSettingsState()
{
  const auto status = capture_.status();
  const bool busy = status.collecting || status.draining;
  const bool open = !closed_ && !profilePending_;
  dock_->settingsShortcut->setEnabled(open);
  dialog_->updateState(!busy && open, busy, draftSettings() != applied_, open, dock_->presets());
}
bool ReplayController::applySettings()
{
  dialog_->budget->interpretText();
  if (closed_ || profilePending_) return false;
  const auto draft = draftSettings();
  if (draft == applied_) return true;
  const auto status = capture_.status();
  if (status.collecting || status.draining) return false;
  if (!persistSettings(draft)) {
    dialog_->showError(error_);
    return false;
  }
  applied_ = draft;
  dialog_->settingsError->hide();
  if (error_ == persistFailed) error_.clear();
  refresh();
  return true;
}
bool ReplayController::saveSettings()
{
  if (loading_ || closed_) return false;
  return persistSettings(applied_);
}
bool ReplayController::persistSettings(const ReplaySettings &settings)
{
  auto *config = obs_frontend_get_profile_config();
  bool saved = false;
  if (config) {
    auto *data = obs_data_create();
    prefs::write(data, settings, dock_->presets());
    hotkeys_->save(data);
    saved = prefs::saveToProfile(config, data);
    obs_data_release(data);
  }
  if (!saved) error(persistFailed);
  return saved;
}
void ReplayController::loadSettings()
{
  loading_ = true;
  auto *data = prefs::loadFromProfile(obs_frontend_get_profile_config());
  prefs::applyDefaults(data);
  // Read back through the widgets, so the applied values have the fields' ranges and precision.
  dialog_->setDraft(prefs::readSettings(data));
  applied_ = draftSettings();
  const auto presets = prefs::readPresets(data);
  dock_->setPresets(presets);
  hotkeys_->setPresetCount(int(presets.size()));
  hotkeys_->load(data);
  obs_data_release(data);
  loading_ = false;
  resetDraft();
}
bool ReplayController::presetsEditable() const
{
  const auto status = capture_.status();
  return !closed_ && !profilePending_ && !status.collecting && !status.draining;
}
void ReplayController::addPreset()
{
  const int count = dock_->presetCount();
  if (!presetsEditable() || count >= maxPresets) return;
  loading_ = true;
  dock_->addPreset(newPreset(count));
  applied_.presetFolders.resize(size_t(count) + 1);
  dialog_->setPresetFolderCount(count + 1); // Keeps the window's unapplied edits.
  hotkeys_->setPresetCount(count + 1);
  loading_ = false;
  saveSettings();
  refresh();
}
// The presets after `index` move up, taking their folders and hotkey bindings with them.
void ReplayController::removePreset(int index)
{
  const int count = dock_->presetCount();
  if (!presetsEditable() || count <= minPresets || index < 0 || index >= count) return;
  loading_ = true;
  auto draft = draftSettings();
  dock_->removePreset(index);
  applied_.presetFolders.resize(size_t(count));
  applied_.presetFolders.erase(applied_.presetFolders.begin() + index);
  if (size_t(index) < draft.presetFolders.size()) draft.presetFolders.erase(draft.presetFolders.begin() + index);
  dialog_->setDraft(draft); // Keeps the window's other unapplied edits.
  hotkeys_->removePreset(index);
  if (saveErrorPreset_ == index)
    saveErrorPreset_ = -1;
  else if (saveErrorPreset_ > index)
    --saveErrorPreset_;
  loading_ = false;
  saveSettings();
  refresh();
}
// OBS saves bindings it loads itself; these are stored with the replay settings,
// so persist them when they change instead of waiting for the next exit.
void ReplayController::bindingsChanged(void *data, calldata_t *params)
{
  auto *self = static_cast<ReplayController *>(data);
  auto *changed = static_cast<obs_hotkey_t *>(calldata_ptr(params, "hotkey"));
  if (!changed || self->loading_ || self->closed_) return;
  if (!self->hotkeys_->contains(obs_hotkey_get_id(changed))) return;
  if (self->bindingSavePending_.exchange(true)) return;
  QMetaObject::invokeMethod(
      self,
      [self] {
        self->bindingSavePending_ = false;
        self->saveSettings();
      },
      Qt::QueuedConnection);
}
void ReplayController::onHotkey(HotkeyAction action, Time pressed)
{
  // Runs on the hotkey thread; the press time was captured before this hand-off.
  QMetaObject::invokeMethod(
      this,
      [this, action, pressed] {
        if (closed_) return;
        if (action == HotkeyAction::Start)
          startBuffer();
        else if (action == HotkeyAction::Stop)
          stopBuffer();
        else
          saveReplay(presetOf(action), pressed);
      },
      Qt::QueuedConnection);
}

void ReplayController::profileChanging()
{
  // OBS resets video and recreates its encoders right after this event, so
  // every encoder this session uses must be released before returning.
  dialog_->reject();
  saveSettings();
  profilePending_ = true;
  capture_.stopAndWait();
  warning_ = "Profile changing: draining previous saves. Replay will remain stopped.";
  refresh();
}
void ReplayController::profileChanged()
{
  loadSettings();
  profilePending_ = false;
  fingerprint_.clear();
  warning_.clear();
  dialog_->encoder->setText(inheritedEncodingText);
  sessionFolders_.clear();
  refresh();
}
void ReplayController::builtInStarted()
{
  if (capture_.status().collecting) {
    stopBuffer();
    error("OBS's built-in replay buffer started. This plugin stopped collection and is draining its accepted saves.");
  }
}
void ReplayController::recordingPaused(bool paused)
{
  capture_.notePause(paused);
  refresh();
}
void ReplayController::shutdown()
{
  if (closed_) return;
  dialog_->reject();
  if (settingsAction_) settingsAction_->setEnabled(false);
  dock_->settingsShortcut->setEnabled(false);
  saveSettings();
  closed_ = true;
  timer_->stop();
  capture_.shutdown();
  signal_handler_disconnect(obs_get_signal_handler(), "hotkey_bindings_changed", bindingsChanged, this);
}

void ReplayController::startBuffer()
{
  if (closed_ || profilePending_) return;
  try {
    auto status = capture_.status();
    if (status.collecting) return;
    if (status.draining) throw std::runtime_error("Accepted saves are draining; wait before starting");
    if (obs_frontend_replay_buffer_active())
      throw std::runtime_error("OBS's built-in replay buffer is active; stop it manually before starting this buffer");
    // A previous session may still hold encoders the frontend has since replaced.
    capture_.releaseIdleOutput();
    auto encoders = recordingEncoders(applied_.shareEncoder);
    Options options;
    options.budget = uint64_t(applied_.budgetGb * bytesPerGb);
    options.shared = encoders.shared;
    options.trackNames = encoders.trackNames;
    options.filenames = encoders.filenames;
    const auto presets = dock_->presets();
    std::vector<std::filesystem::path> folders(presets.size());
    for (size_t i = 0; i < presets.size(); ++i) {
      const auto preset = i < applied_.presetFolders.size() ? applied_.presetFolders[i] : std::string();
      const auto &configured = applied_.separateFolders ? preset : applied_.folder;
      folders[i] = q(configured).trimmed().isEmpty() ? encoders.folder : std::filesystem::u8path(configured);
    }
    options.folder = folders[0];
    if (!applied_.separateFolders && !std::filesystem::is_directory(options.folder))
      throw std::runtime_error("Replay destination must be an existing directory");
    int longest = 0;
    for (const auto &preset : presets) longest = std::max(longest, preset.seconds);
    options.longest = fromSeconds(longest);
    saveSettings();
    capture_.start(options, encoders.video, encoders.audio);
    fingerprint_ = encoders.fingerprint;
    videoInfo_ = encoders.videoInfo;
    sessionFolders_ = std::move(folders);
    sharedVideo_ = encoders.shared ? encoders.video : nullptr;
    shareLost_ = false;
    if (encoders.shared && obs_frontend_recording_paused()) capture_.notePause(true);
    std::vector<std::string> names;
    for (const auto &preset : presets) names.push_back(preset.name);
    dialog_->encoder->setText(
        q(encoders.description + text::destinations(applied_.separateFolders, names, sessionFolders_)));
    error_.clear();
    saveError_ = false;
    saveErrorPreset_ = -1;
    warning_.clear();
    feedback_.clear();
    feedbackDetail_.clear();
    if (!encoders.shareNote.empty())
      warning_ = "Encoder sharing is unavailable (" + q(encoders.shareNote) +
                 "). This session uses an independent replay encoder.";
  } catch (const std::exception &e) {
    error(e.what());
  }
  refresh();
}
void ReplayController::stopBuffer()
{
  capture_.stop();
  refresh();
}
void ReplayController::saveReplay(int preset, Time endpoint)
{
  // A hotkey press can be queued just before its preset is removed.
  if (preset < 0 || preset >= dock_->presetCount()) return;
  const auto &row = dock_->rows[size_t(preset)];
  const auto name = row.name->text();
  const auto label = q(text::presetLabel(preset, utf8(name)) + ": ");
  try {
    // Presets cannot change during a session, so a buffering session has a folder for each.
    const auto folder =
        size_t(preset) < sessionFolders_.size() ? sessionFolders_[size_t(preset)] : std::filesystem::path();
    const auto id = capture_.save(fromSeconds(row.seconds->value()), endpoint, folder);
    pendingPresets_[id] = {preset, label};
    feedback_ = name + " accepted";
    feedbackDetail_ = "Saving the requested replay.";
    feedbackUntil_ = clockNow() + feedbackDuration;
  } catch (const std::exception &e) {
    error(utf8(label + q(e.what())), true);
    saveErrorPreset_ = preset;
  }
  refresh();
}
void ReplayController::refresh()
{
  if (closed_) return;
  capture_.poll();
  const auto status = capture_.status();
  const bool busy = status.collecting || status.draining;
  dock_->start->setText(status.draining ? "Draining…" : status.collecting ? "Stop Buffering" : "Start Buffering");
  dock_->start->setEnabled(!status.draining && !profilePending_);
  dock_->setActive(status.collecting);
  Time longest = 0;
  const bool editable = presetsEditable();
  const int count = dock_->presetCount();
  for (const auto &row : dock_->rows) {
    const Time requested = fromSeconds(row.seconds->value());
    longest = std::max(longest, requested);
    row.seconds->setEnabled(!busy);
    row.name->setEnabled(!busy);
    row.save->setToolTip(q(text::saveTooltip(requested, status.available)));
    row.save->setEnabled(status.collecting && status.available > 0);
    row.remove->setEnabled(editable && count > minPresets);
  }
  dock_->add->setEnabled(editable && count < maxPresets);
  updateSettingsState();
  if (!busy) sharedVideo_ = nullptr;
  dialog_->details->setText(
      q(text::details(status.available, longest, status.memory, applied_.budgetGb, status.queued, status.active)));
  if (++ticks_ % encoderCheckEveryTicks == 0 && status.collecting) checkRecordingChanges();
  for (const auto &saved : capture_.takeCompleted()) handleCompleted(saved);

  text::StatusInputs inputs;
  inputs.paused = status.paused;
  inputs.draining = status.draining;
  inputs.collecting = status.collecting;
  inputs.degraded = status.degraded;
  inputs.captureMessage = status.message;
  inputs.captureFailed = status.failed;
  inputs.error = utf8(error_);
  inputs.warning = utf8(warning_);
  inputs.feedback = utf8(feedback_);
  inputs.feedbackDetail = utf8(feedbackDetail_);
  inputs.feedbackCurrent = clockNow() < feedbackUntil_;
  const auto shown = text::status(inputs);
  dock_->state->setText(q(shown.state));
  dock_->state->setToolTip(q(shown.tooltip));
  dialog_->notice->setText(q(shown.notice));
  dialog_->notice->setVisible(!dialog_->notice->text().trimmed().isEmpty());
}
// Runs every few refreshes while buffering: the session keeps the encoding it
// started with, so tell the user when OBS's recording setup moves away from it.
void ReplayController::checkRecordingChanges()
{
  try {
    if (sharedVideo_ && !shareLost_) {
      // Applying OBS settings recreates the frontend's encoders; this session keeps the old ones.
      auto *current = frontendRecordingVideo();
      if (current != sharedVideo_) {
        shareLost_ = true;
        error(shareLostText);
      }
      if (current) obs_encoder_release(current);
    }
    // Shown on every check, so it must not hide the lost-sharing warning above.
    if (shareLost_)
      warning_ = shareLostText;
    else if (recordingFingerprint() != fingerprint_)
      warning_ = "Recording settings changed. This session retains its original encoding; restart buffering "
                 "to apply them.";
    obs_video_info current{};
    obs_get_video_info(&current);
    if (!sameVideoFormat(current, videoInfo_)) {
      capture_.stop();
      error("OBS video format changed; previous accepted saves are draining. Restart buffering manually.");
    }
  } catch (const std::exception &e) {
    error(e.what());
  }
}
void ReplayController::handleCompleted(const Result &saved)
{
#ifdef REPLAY_INTEGRATION_TEST
  testResults_.push_back(saved);
#endif
  if (saved.id <= lastResultId_) return;
  lastResultId_ = saved.id;
  const auto request = pendingPresets_.find(saved.id);
  const int presetIndex = request == pendingPresets_.end() ? -1 : request->second.index;
  const QString preset = request == pendingPresets_.end() ? "" : request->second.label;
  if (request != pendingPresets_.end()) pendingPresets_.erase(request);
  feedbackDetail_ = q("Requested " + text::seconds(saved.requested) + ", actual " + text::seconds(saved.actual));
  if (!saved.partial.empty()) feedbackDetail_ += "\nPartial: " + q(saved.partial);
  if (!saved.error.empty()) {
    error(utf8(preset + q(saved.error)), true);
    saveErrorPreset_ = presetIndex;
    return;
  }
  // Another preset succeeding does not repair this preset's destination.
  if (saveError_ && (saveErrorPreset_ < 0 || saveErrorPreset_ == presetIndex)) {
    error_.clear();
    saveError_ = false;
    saveErrorPreset_ = -1;
  }
  feedback_ = "Saved " + pathText(saved.path.filename());
  feedbackDetail_ += "\n" + pathText(saved.path);
  feedbackUntil_ = clockNow() + feedbackDuration;
}
void ReplayController::error(const std::string &message, bool saveError)
{
  error_ = q(message);
  saveError_ = saveError;
  if (!saveError) saveErrorPreset_ = -1;
  blog(LOG_WARNING, "[multi-replay] %s", message.c_str());
#ifdef REPLAY_INTEGRATION_TEST
  errors_.push_front(q(message));
  if (errors_.size() > 20) errors_.pop_back();
#endif
}
} // namespace replay
