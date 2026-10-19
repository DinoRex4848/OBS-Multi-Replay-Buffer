#include "settings.hpp"
#include <util/bmem.h>
#include <QApplication>
#include <QAction>
#ifdef REPLAY_INTEGRATION_TEST
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QKeyEvent>
#include <QMainWindow>
#endif
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <atomic>
#include <unordered_map>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-multi-replay", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
  return "Save multiple replay lengths from one encoded RAM history";
}
MODULE_EXPORT const char *obs_module_name(void) { return "Multi-Length Replay"; }
MODULE_EXPORT const char *obs_module_author(void) { return "Multi-Length Replay contributors"; }

namespace {
using namespace replay;
constexpr const char *section = "MultiLengthReplay";
constexpr const char *dockId = "obs-multi-replay-dock";
QString q(const std::string &text) { return QString::fromUtf8(text.c_str()); }
QString pathText(const std::filesystem::path &path) { return q(path.u8string()); }
std::filesystem::path filePath(const QString &text) { return std::filesystem::u8path(text.toUtf8().constData()); }
QString durationText(Time usec) { return QString::number(double(usec) / 1000000, 'f', 1) + " s"; }
// A long error or filename must not increase the dock's minimum width/height.
class ElidedLabel : public QLabel {
public:
  explicit ElidedLabel(QWidget *parent) : QLabel(parent)
  {
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setMinimumWidth(0);
  }
  void setText(const QString &text)
  {
    fullText_ = text;
    setToolTip(text);
    updateText();
  }
  const QString &fullText() const { return fullText_; }

protected:
  void resizeEvent(QResizeEvent *event) override
  {
    QLabel::resizeEvent(event);
    updateText();
  }
  void changeEvent(QEvent *event) override
  {
    QLabel::changeEvent(event);
    updateText();
  }

private:
  void updateText()
  {
    QLabel::setText(fontMetrics().elidedText(fullText_.section('\n', 0, 0), Qt::ElideRight, contentsRect().width()));
  }
  QString fullText_;
};
struct BufferSettings {
  double budgetGb = 6;
  QString folder;
  bool shareEncoder = true;
  bool separateFolders = false;
  std::array<QString, 3> presetFolders{};
  bool operator==(const BufferSettings &other) const
  {
    return budgetGb == other.budgetGb && folder == other.folder && shareEncoder == other.shareEncoder &&
           separateFolders == other.separateFolders && presetFolders == other.presetFolders;
  }
};
struct HotkeyContext {
  class ReplayController *dock;
  int action;
};
class ReplayController : public QObject {
public:
  explicit ReplayController(QWidget *parent) : QObject(parent)
  {
    controls_ = new QWidget(parent);
    settings_ = new QDialog(parent);
    settings_->setObjectName("multiReplaySettingsDialog");
    settings_->setWindowTitle("Multi-Buffer Settings");
    settings_->resize(600, 480);
    auto *windowLayout = new QVBoxLayout(settings_);
    tabs_ = new QTabWidget(settings_);
    windowLayout->addWidget(tabs_);
    auto addPage = [this](const char *title) {
      auto *scroll = new QScrollArea(tabs_);
      scroll->setWidgetResizable(true);
      scroll->setFrameShape(QFrame::NoFrame);
      auto *page = new QWidget(scroll);
      scroll->setWidget(page);
      tabs_->addTab(scroll, title);
      return page;
    };
    auto *settingsPage = addPage("Settings");
    diagnostics_ = addPage("Diagnostics");
    settingsLock_ =
        new QLabel("Stop buffering and wait for accepted saves to finish before changing settings.", settings_);
    settingsLock_->setWordWrap(true);
    windowLayout->addWidget(settingsLock_);
    settingsError_ = new QLabel(settings_);
    settingsError_->setWordWrap(true);
    settingsError_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    settingsError_->hide();
    windowLayout->addWidget(settingsError_);
    settingsButtons_ =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, settings_);
    windowLayout->addWidget(settingsButtons_);
    connect(settingsButtons_, &QDialogButtonBox::accepted, this, [this] {
      if (applySettings()) settings_->accept();
    });
    connect(settingsButtons_, &QDialogButtonBox::rejected, settings_, &QDialog::reject);
    connect(settingsButtons_->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            [this] { applySettings(); });
    connect(settings_, &QDialog::finished, this, [this] { resetDraft(); });
    auto *layout = new QVBoxLayout(controls_);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    start_ = new QPushButton("Start Buffering", controls_);
    start_->setObjectName("replayBufferButton");
    layout->addWidget(start_);
    connect(start_, &QPushButton::clicked, this, [this] {
      if (capture_.status().collecting)
        stopBuffer();
      else
        startBuffer();
    });
    auto *presets = new QGridLayout;
    presets->setHorizontalSpacing(6);
    presets->setVerticalSpacing(6);
    presets->setColumnStretch(0, 1);
    presets->addWidget(new QLabel("Preset"), 0, 0);
    presets->addWidget(new QLabel("Seconds"), 0, 1);
    for (int i = 0; i < 3; ++i) {
      names_[i] = new QLineEdit("Replay " + QString::number(i + 1), controls_);
      names_[i]->setMinimumWidth(0);
      names_[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
      seconds_[i] = new QSpinBox(controls_);
      seconds_[i]->setRange(1, 86400);
      seconds_[i]->setValue(std::array<int, 3>{60, 240, 600}[i]);
      saves_[i] = new QPushButton("Save", controls_);
      presets->addWidget(names_[i], i + 1, 0);
      presets->addWidget(seconds_[i], i + 1, 1);
      presets->addWidget(saves_[i], i + 1, 2);
      connect(saves_[i], &QPushButton::clicked, this, [this, i] { saveReplay(i, clockNow()); });
    }
    layout->addLayout(presets);
    state_ = new ElidedLabel(controls_);
    layout->addWidget(state_);
    layout->addStretch();
    settingsShortcut_ = new QPushButton("Settings", controls_);
    settingsShortcut_->setObjectName("multiReplaySettingsButton");
    settingsShortcut_->setToolTip("Open Multi-Buffer Settings");
    layout->addWidget(settingsShortcut_, 0, Qt::AlignRight);
    connect(settingsShortcut_, &QPushButton::clicked, this, [this] { showSettings(); });
    auto *settingsLayout = new QVBoxLayout(settingsPage);
    settingsLayout->setContentsMargins(8, 8, 8, 8);
    settingsLayout->setSpacing(6);
    budget_ = new QDoubleSpinBox(settingsPage);
    budget_->setRange(0.05, 256);
    budget_->setDecimals(2);
    budget_->setValue(6);
    budget_->setSuffix(" GB");
    budget_->setToolTip("Decimal GB of replay memory; excludes OBS and encoder allocations. Change while stopped.");
    settingsLayout->addWidget(new QLabel("Replay RAM budget", settingsPage));
    settingsLayout->addWidget(budget_);
    auto *folderRow = new QWidget(settingsPage);
    auto *folderLayout = new QHBoxLayout(folderRow);
    folderLayout->setContentsMargins(0, 0, 0, 0);
    folder_ = new QLineEdit(folderRow);
    folder_->setMinimumWidth(0);
    folder_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    folder_->setPlaceholderText("Use OBS recording folder");
    browse_ = new QPushButton("Browse…", folderRow);
    folderLayout->addWidget(folder_, 1);
    folderLayout->addWidget(browse_);
    settingsLayout->addWidget(new QLabel("Save folder", settingsPage));
    settingsLayout->addWidget(folderRow);
    connect(browse_, &QPushButton::clicked, this, [this] {
      auto value = QFileDialog::getExistingDirectory(settings_, "Replay destination", folder_->text());
      if (!value.isEmpty()) folder_->setText(value);
    });
    separateFolders_ = new QCheckBox("Use separate folders for each preset", settingsPage);
    separateFolders_->setToolTip("Each blank preset folder uses OBS's recording folder. The shared save folder is used "
                                 "only when separate folders are turned off.");
    settingsLayout->addWidget(separateFolders_);
    presetFoldersView_ = new QWidget(settingsPage);
    auto *presetFoldersLayout = new QVBoxLayout(presetFoldersView_);
    presetFoldersLayout->setContentsMargins(0, 0, 0, 0);
    auto *folderHint = new QLabel("Leave a preset folder blank to use OBS's recording folder.", presetFoldersView_);
    folderHint->setWordWrap(true);
    presetFoldersLayout->addWidget(folderHint);
    for (int i = 0; i < 3; ++i) {
      presetFolderLabels_[i] = new QLabel(presetFoldersView_);
      presetFolderLabels_[i]->setTextFormat(Qt::PlainText);
      presetFolderLabels_[i]->setWordWrap(true);
      presetFoldersLayout->addWidget(presetFolderLabels_[i]);
      auto *row = new QWidget(presetFoldersView_);
      auto *rowLayout = new QHBoxLayout(row);
      rowLayout->setContentsMargins(0, 0, 0, 0);
      presetFolders_[i] = new QLineEdit(row);
      presetFolders_[i]->setMinimumWidth(0);
      presetFolders_[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
      presetFolders_[i]->setPlaceholderText("Use OBS recording folder");
      presetBrowse_[i] = new QPushButton("Browse…", row);
      rowLayout->addWidget(presetFolders_[i], 1);
      rowLayout->addWidget(presetBrowse_[i]);
      presetFoldersLayout->addWidget(row);
      connect(presetBrowse_[i], &QPushButton::clicked, this, [this, i] {
        auto value = QFileDialog::getExistingDirectory(
            settings_, "Replay preset " + QString::number(i + 1) + " destination", presetFolders_[i]->text());
        if (!value.isEmpty()) presetFolders_[i]->setText(value);
      });
    }
    settingsLayout->addWidget(presetFoldersView_);
    separateEncoder_ = new QCheckBox("Use separate replay encoder", settingsPage);
    separateEncoder_->setToolTip(
        "Leave unchecked to share OBS's recording encoders (default). Check to use separate replay encoders;\n"
        "recording and replay then run independently and require two video encodes when both are active.\n"
        "When sharing, pausing the recording also pauses replay collection (paused time is cut from replays),\n"
        "and whichever starts second begins at the encoder's next keyframe.");
    settingsLayout->addWidget(separateEncoder_);
    auto *diagnosticsLayout = new QVBoxLayout(diagnostics_);
    diagnosticsLayout->setContentsMargins(8, 8, 8, 8);
    details_ = new QLabel(diagnostics_);
    details_->setWordWrap(true);
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    diagnosticsLayout->addWidget(details_);
    encoder_ = new QLabel("Encoding is inherited from OBS recording settings.", diagnostics_);
    encoder_->setWordWrap(true);
    encoder_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    diagnosticsLayout->addWidget(encoder_);
    notice_ = new QLabel(diagnostics_);
    notice_->setWordWrap(true);
    notice_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    diagnosticsLayout->addWidget(notice_);
    diagnosticsLayout->addStretch();
    settingsLayout->addStretch();
    for (int i = 0; i < 5; ++i) {
      contexts_[i] = {this, i};
      const std::array<const char *, 5> ids = {"MultiReplay.Start", "MultiReplay.Stop", "MultiReplay.Preset1",
                                               "MultiReplay.Preset2", "MultiReplay.Preset3"};
      const std::array<const char *, 5> descriptions = {
          "Multi-Length Replay: Start Buffering", "Multi-Length Replay: Stop Buffering",
          "Multi-Length Replay: Save Replay 1", "Multi-Length Replay: Save Replay 2",
          "Multi-Length Replay: Save Replay 3"};
      hotkeys_[i] = obs_hotkey_register_frontend(ids[i], descriptions[i], hotkey, &contexts_[i]);
    }
    loadSettings();
    for (int i = 0; i < 3; ++i) {
      connect(seconds_[i], qOverload<int>(&QSpinBox::valueChanged), this, [this] { saveSettings(); });
      connect(names_[i], &QLineEdit::editingFinished, this, [this] { saveSettings(); });
    }
    connect(budget_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { updateSettingsState(); });
    connect(folder_, &QLineEdit::textChanged, this, [this] { updateSettingsState(); });
    connect(separateEncoder_, &QCheckBox::toggled, this, [this] { updateSettingsState(); });
    connect(separateFolders_, &QCheckBox::toggled, this, [this] { updateSettingsState(); });
    for (auto *field : presetFolders_) connect(field, &QLineEdit::textChanged, this, [this] { updateSettingsState(); });
    signal_handler_connect(obs_get_signal_handler(), "hotkey_bindings_changed", bindingsChanged, this);
    timer_ = new QTimer(this);
    timer_->setInterval(250);
    connect(timer_, &QTimer::timeout, this, [this] { refresh(); });
    timer_->start();
    refresh();
  }
  ~ReplayController() override
  {
    shutdown();
    delete settingsAction_.data();
    for (auto id : hotkeys_) obs_hotkey_unregister(id);
    delete controls_.data();
    delete settings_.data();
  }
  QWidget *controls() const { return controls_; }
  bool addSettingsMenu()
  {
    settingsAction_ = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("Multi-Buffer Settings"));
    if (!settingsAction_) return false;
    settingsAction_->setObjectName("multiReplaySettingsAction");
    connect(settingsAction_, &QAction::triggered, this, [this] { showSettings(); });
    return true;
  }
  void showSettings()
  {
    if (closed_ || profilePending_) return;
    if (!settings_->isVisible()) {
      resetDraft();
      tabs_->setCurrentIndex(0);
    }
    refresh();
    settings_->show();
    settings_->raise();
    settings_->activateWindow();
  }
  BufferSettings draftSettings() const
  {
    return {budget_->value(),
            folder_->text(),
            !separateEncoder_->isChecked(),
            separateFolders_->isChecked(),
            {presetFolders_[0]->text(), presetFolders_[1]->text(), presetFolders_[2]->text()}};
  }
  void resetDraft()
  {
    const QSignalBlocker budgetBlock(budget_), folderBlock(folder_), shareBlock(separateEncoder_),
        separateBlock(separateFolders_);
    budget_->setValue(applied_.budgetGb);
    folder_->setText(applied_.folder);
    separateEncoder_->setChecked(!applied_.shareEncoder);
    separateFolders_->setChecked(applied_.separateFolders);
    for (int i = 0; i < 3; ++i) {
      const QSignalBlocker block(presetFolders_[i]);
      presetFolders_[i]->setText(applied_.presetFolders[i]);
    }
    settingsError_->clear();
    settingsError_->hide();
    updateSettingsState();
  }
  void updateSettingsState()
  {
    const auto status = capture_.status();
    const bool busy = status.collecting || status.draining;
    const bool editable = !busy && !closed_ && !profilePending_;
    const bool dirty = !(draftSettings() == applied_);
    settingsShortcut_->setEnabled(!closed_ && !profilePending_);
    budget_->setEnabled(editable);
    const bool separate = separateFolders_->isChecked();
    folder_->setEnabled(editable && !separate);
    browse_->setEnabled(editable && !separate);
    separateFolders_->setEnabled(editable);
    presetFoldersView_->setVisible(separate);
    for (int i = 0; i < 3; ++i) {
      presetFolders_[i]->setEnabled(editable && separate);
      presetBrowse_[i]->setEnabled(editable && separate);
      presetFolderLabels_[i]->setText(
          QString("Preset %1 — %2 (%3 s)").arg(i + 1).arg(names_[i]->text()).arg(seconds_[i]->value()));
    }
    separateEncoder_->setEnabled(editable);
    settingsLock_->setVisible(busy);
    settingsButtons_->button(QDialogButtonBox::Apply)->setEnabled(editable && dirty);
    settingsButtons_->button(QDialogButtonBox::Ok)->setEnabled(!closed_ && !profilePending_ && (!dirty || editable));
  }
  bool applySettings()
  {
    budget_->interpretText();
    if (closed_ || profilePending_) return false;
    const auto draft = draftSettings();
    if (draft == applied_) return true;
    const auto status = capture_.status();
    if (status.collecting || status.draining) return false;
    if (!persistSettings(draft)) {
      settingsError_->setText(error_);
      settingsError_->show();
      return false;
    }
    applied_ = draft;
    settingsError_->hide();
    if (error_ == "Could not persist replay settings to the current OBS profile") error_.clear();
    refresh();
    return true;
  }
  // OBS saves bindings it loads itself; these are stored with the replay settings,
  // so persist them when they change instead of waiting for the next exit.
  static void bindingsChanged(void *data, calldata_t *params)
  {
    auto *self = static_cast<ReplayController *>(data);
    auto *changed = static_cast<obs_hotkey_t *>(calldata_ptr(params, "hotkey"));
    if (!changed || self->loading_ || self->closed_) return;
    const auto id = obs_hotkey_get_id(changed);
    if (std::find(self->hotkeys_.begin(), self->hotkeys_.end(), id) == self->hotkeys_.end()) return;
    if (self->bindingSavePending_.exchange(true)) return;
    QMetaObject::invokeMethod(
        self,
        [self] {
          self->bindingSavePending_ = false;
          self->saveSettings();
        },
        Qt::QueuedConnection);
  }
  static void hotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
  {
    if (!pressed) return;
    auto *ctx = static_cast<HotkeyContext *>(data);
    // Capture media ending time before queued delivery to the OBS UI thread.
    const Time endpoint = clockNow();
    const int action = ctx->action;
    QMetaObject::invokeMethod(
        ctx->dock,
        [dock = ctx->dock, action, endpoint] {
          if (dock->closed_) return;
          if (action == 0)
            dock->startBuffer();
          else if (action == 1)
            dock->stopBuffer();
          else
            dock->saveReplay(action - 2, endpoint);
        },
        Qt::QueuedConnection);
  }
  bool saveSettings()
  {
    if (loading_ || closed_) return false;
    return persistSettings(applied_);
  }
  bool persistSettings(const BufferSettings &value)
  {
    auto *config = obs_frontend_get_profile_config();
    if (!config) {
      error("Could not persist replay settings to the current OBS profile");
      return false;
    }
    auto *data = obs_data_create();
    obs_data_set_double(data, "budget_gb", value.budgetGb);
    obs_data_set_string(data, "folder", value.folder.toUtf8().constData());
    obs_data_set_bool(data, "share_encoder", value.shareEncoder);
    obs_data_set_bool(data, "separate_folders", value.separateFolders);
    for (int i = 0; i < 3; ++i) {
      obs_data_set_string(data, ("preset_folder" + std::to_string(i)).c_str(),
                          value.presetFolders[i].toUtf8().constData());
      obs_data_set_int(data, ("seconds" + std::to_string(i)).c_str(), seconds_[i]->value());
      obs_data_set_string(data, ("name" + std::to_string(i)).c_str(), names_[i]->text().toUtf8().constData());
    }
    for (int i = 0; i < 5; ++i) {
      auto *bindings = obs_hotkey_save(hotkeys_[i]);
      obs_data_set_array(data, ("hotkey" + std::to_string(i)).c_str(), bindings);
      obs_data_array_release(bindings);
    }
    const bool hadValue = config_has_user_value(config, section, "Settings");
    const char *previous = config_get_string(config, section, "Settings");
    const std::string previousJson = previous ? previous : "";
    config_set_string(config, section, "Settings", obs_data_get_json(data));
    obs_data_release(data);
    if (config_save_safe(config, "tmp", "bak") != CONFIG_SUCCESS) {
      // OBS also saves this in-memory config. Never leave rejected edits in it.
      if (hadValue)
        config_set_string(config, section, "Settings", previousJson.c_str());
      else
        config_remove_value(config, section, "Settings");
      error("Could not persist replay settings to the current OBS profile");
      return false;
    }
    return true;
  }
  void loadSettings()
  {
    loading_ = true;
    auto *config = obs_frontend_get_profile_config();
    const char *json = config ? config_get_string(config, section, "Settings") : nullptr;
    auto *data = json ? obs_data_create_from_json(json) : obs_data_create();
    if (!data) data = obs_data_create();
    obs_data_set_default_double(data, "budget_gb", 6);
    obs_data_set_default_string(data, "folder", "");
    obs_data_set_default_bool(data, "share_encoder", true);
    budget_->setValue(obs_data_get_double(data, "budget_gb"));
    folder_->setText(q(obs_data_get_string(data, "folder")));
    separateEncoder_->setChecked(!obs_data_get_bool(data, "share_encoder"));
    separateFolders_->setChecked(obs_data_get_bool(data, "separate_folders"));
    for (int i = 0; i < 3; ++i)
      presetFolders_[i]->setText(q(obs_data_get_string(data, ("preset_folder" + std::to_string(i)).c_str())));
    applied_ = draftSettings();
    for (int i = 0; i < 3; ++i) {
      const std::string key = "seconds" + std::to_string(i), name = "name" + std::to_string(i);
      obs_data_set_default_int(data, key.c_str(), std::array<int, 3>{60, 240, 600}[i]);
      obs_data_set_default_string(data, name.c_str(), ("Replay " + std::to_string(i + 1)).c_str());
      seconds_[i]->setValue(int(obs_data_get_int(data, key.c_str())));
      names_[i]->setText(q(obs_data_get_string(data, name.c_str())));
    }
    for (int i = 0; i < 5; ++i) {
      auto *bindings = obs_data_get_array(data, ("hotkey" + std::to_string(i)).c_str());
      if (!bindings) bindings = obs_data_array_create();
      obs_hotkey_load(hotkeys_[i], bindings);
      obs_data_array_release(bindings);
    }
    obs_data_release(data);
    loading_ = false;
    resetDraft();
  }
  void profileChanging()
  {
    // OBS resets video and recreates its encoders right after this event, so
    // every encoder this session uses must be released before returning.
    settings_->reject();
    saveSettings();
    profilePending_ = true;
    capture_.stopAndWait();
    refresh();
    warning_ = "Profile changing: draining previous saves. Replay will remain stopped.";
    refresh();
  }
  void profileChanged()
  {
    loadSettings();
    profilePending_ = false;
    fingerprint_.clear();
    warning_.clear();
    encoder_->setText("Encoding is inherited from OBS recording settings.");
    sessionFolders_.fill({});
    refresh();
  }
  void builtInStarted()
  {
    if (capture_.status().collecting) {
      stopBuffer();
      error("OBS's built-in replay buffer started. This plugin stopped collection and is draining its accepted saves.");
    }
  }
  void recordingPaused(bool paused)
  {
    capture_.notePause(paused);
    refresh();
  }
  void shutdown()
  {
    if (closed_) return;
    settings_->reject();
    if (settingsAction_) settingsAction_->setEnabled(false);
    settingsShortcut_->setEnabled(false);
    saveSettings();
    closed_ = true;
    timer_->stop();
    capture_.shutdown();
    signal_handler_disconnect(obs_get_signal_handler(), "hotkey_bindings_changed", bindingsChanged, this);
  }
#ifdef REPLAY_INTEGRATION_TEST
  void runIntegrationTest()
  {
    auto output = qEnvironmentVariable("REPLAY_TEST_OUTPUT");
    if (output.isEmpty()) return;
    if (qEnvironmentVariable("REPLAY_TEST_FOLDERS") == "restore") {
      QTimer::singleShot(500, this, [this, output] {
        const auto base = qEnvironmentVariable("REPLAY_TEST_FOLDER_BASE");
        testCheck(applied_.separateFolders && applied_.presetFolders[0] == base + "/Preset 1 α" &&
                      applied_.presetFolders[1] == base + "/Preset 2 β" &&
                      applied_.presetFolders[2] == base + "/Preset 3 γ",
                  "preset folders survive restart");
        testNote("restored preset folders");
        testFinish(output);
      });
      return;
    }
    if (qEnvironmentVariable("REPLAY_TEST_LAYOUT") == "restore") {
      QTimer::singleShot(500, this, [this, output] {
        testNote("restored layout and profile");
        testFinish(output);
      });
      return;
    }
    separateFolders_->setChecked(false);
    for (auto *field : presetFolders_) field->clear();
    folder_->setText(output);
    seconds_[0]->setValue(2);
    seconds_[1]->setValue(4);
    seconds_[2]->setValue(6);
    budget_->setValue(0.25);
    separateEncoder_->setChecked(!qEnvironmentVariableIsSet("REPLAY_TEST_SHARED"));
    if (qEnvironmentVariable("REPLAY_TEST_SHARED") == "long") {
      seconds_[0]->setValue(60);
      seconds_[1]->setValue(240);
      seconds_[2]->setValue(600);
      budget_->setValue(16);
    }
    applySettings();
    if (qEnvironmentVariableIsSet("REPLAY_TEST_FOLDERS")) {
      runFolderTest(output);
      return;
    }
    if (qEnvironmentVariableIsSet("REPLAY_TEST_SETTINGS")) {
      runSettingsTest(output);
      return;
    }
    if (qEnvironmentVariableIsSet("REPLAY_TEST_LAYOUT")) {
      runLayoutTest(output);
      return;
    }
    if (applied_.shareEncoder) {
      runSharedTest(output);
      return;
    }
    QTimer::singleShot(500, this, [this] { startBuffer(); });
    QTimer::singleShot(4000, this, [this] { obs_hotkey_trigger_routed_callback(hotkeys_[2], true); });
    QTimer::singleShot(6500, this, [this] {
      for (int key : {2, 3, 2, 4}) obs_hotkey_trigger_routed_callback(hotkeys_[key], true);
    });
    QTimer::singleShot(8500, this, [this, output] { controls_->grab().save(output + "/dock.png"); });
    QTimer::singleShot(10000, this, [this] { stopBuffer(); });
    QTimer::singleShot(13000, this, [this, output] {
      testNote("independent stopped");
      testFinish(output);
    });
  }
  // A main window hidden in the tray accepts close() without shutting OBS down.
  static void closeObs()
  {
    auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
    if (!main->isVisible()) main->showMinimized();
    main->close();
  }
  void testNote(const char *step)
  {
    auto status = capture_.status();
    QJsonObject value;
    value["step"] = step;
    value["collecting"] = status.collecting;
    value["shared"] = status.shared;
    value["paused"] = status.paused;
    value["available"] = double(status.available) / 1e6;
    value["memory_mb"] = double(status.memory) / 1e6;
    value["recording"] = obs_frontend_recording_active();
    value["state"] = state_->fullText();
    value["notice"] = notice_->text();
    value["button_class"] = start_->property("class").toString();
    value["controls_visible"] = controls_->isVisible();
    value["settings_visible"] = settings_->isVisible();
    value["settings_shortcut_visible"] = settingsShortcut_->isVisible();
    value["settings_shortcut_enabled"] = settingsShortcut_->isEnabled();
    value["settings_tab"] = tabs_->currentIndex();
    value["settings_tabs"] = tabs_->count();
    value["settings_dirty"] = !(draftSettings() == applied_);
    value["apply_enabled"] = settingsButtons_->button(QDialogButtonBox::Apply)->isEnabled();
    value["applied_budget"] = applied_.budgetGb;
    value["applied_folder"] = applied_.folder;
    value["applied_share_encoder"] = applied_.shareEncoder;
    value["separate_encoder_checked"] = separateEncoder_->isChecked();
    value["separate_folders"] = applied_.separateFolders;
    QJsonArray destinations;
    for (const auto &path : sessionFolders_) destinations.append(pathText(path));
    value["destinations"] = destinations;
    value["settings_height"] = settings_->height();
    value["settings_minimum_height"] = settings_->minimumSizeHint().height();
    value["diagnostics_fit"] = details_->height() >= details_->heightForWidth(details_->width()) &&
                               encoder_->height() >= encoder_->heightForWidth(encoder_->width());
    value["settings_locked"] = !budget_->isEnabled() && !folder_->isEnabled() && !separateEncoder_->isEnabled();
    value["width"] = controls_->width();
    value["height"] = controls_->height();
    value["minimum_width"] = controls_->minimumSizeHint().width();
    value["minimum_height"] = controls_->minimumSizeHint().height();
    value["preset_seconds"] = QJsonArray{seconds_[0]->value(), seconds_[1]->value(), seconds_[2]->value()};
    value["save_tooltip"] = saves_[0]->toolTip();
    value["notice_tooltip"] = state_->toolTip();
    value["last_result_id"] = double(lastResultId_);
    bool contained = controls_->rect().contains(start_->geometry()) && controls_->rect().contains(state_->geometry()) &&
                     controls_->rect().contains(settingsShortcut_->geometry());
    for (int i = 0; i < 3; ++i)
      contained &= controls_->rect().contains(names_[i]->geometry()) &&
                   controls_->rect().contains(seconds_[i]->geometry()) &&
                   controls_->rect().contains(saves_[i]->geometry());
    value["controls_contained"] = contained;
    auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
    auto *controlsDock = main->findChild<QDockWidget *>(dockId);
    value["replay_dock_registered"] = controlsDock != nullptr;
    value["settings_dock_registered"] = main->findChild<QDockWidget *>("obs-multi-replay-settings-dock") != nullptr;
    value["settings_action_registered"] =
        settingsAction_ && main->findChildren<QAction *>("multiReplaySettingsAction").size() == 1;
    value["settings_windows"] = main->findChildren<QDialog *>("multiReplaySettingsDialog").size();
    value["controls_docked_right"] =
        controlsDock && !controlsDock->isFloating() && main->dockWidgetArea(controlsDock) == Qt::RightDockWidgetArea;
    const auto color = start_->grab().toImage().pixelColor(start_->width() / 2, 5);
    value["buffer_button_color"] = color.name();
    if (auto *record = main->findChild<QPushButton *>("recordButton")) {
      value["record_button_class"] = record->property("class").toString();
      value["record_button_color"] = record->grab().toImage().pixelColor(record->width() / 2, 5).name();
    }
    steps_.append(value);
  }
  void testTheme(const QString &variant)
  {
    QTimer::singleShot(200, this, [this, variant] {
      auto *dialog = QApplication::activeModalWidget();
      auto *themes = dialog ? dialog->findChild<QComboBox *>("themeVariant") : nullptr;
      auto *buttons = dialog ? dialog->findChild<QDialogButtonBox *>("buttonBox") : nullptr;
      const int index = themes ? themes->findData(variant) : -1;
      if (index < 0 || !buttons) error("UI test could not select theme " + variant.toStdString());
      if (index >= 0) {
        themes->setCurrentIndex(index);
        QMetaObject::invokeMethod(dialog, "on_themeVariant_activated", Qt::DirectConnection, Q_ARG(int, index));
      }
      if (buttons)
        buttons->button(QDialogButtonBox::Ok)->click();
      else if (dialog)
        dialog->close();
    });
    QMetaObject::invokeMethod(static_cast<QObject *>(obs_frontend_get_main_window()), "on_action_Settings_triggered",
                              Qt::QueuedConnection);
  }
  void runLayoutTest(const QString &output)
  {
    auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
    auto *controlsDock = main->findChild<QDockWidget *>(dockId);
    if (!controlsDock || !settingsAction_) {
      error("UI test could not find replay dock and settings action");
      testFinish(output);
      return;
    }
    controlsDock->setFloating(true);
    controlsDock->show();
    controlsDock->resize(320, 300 + controlsDock->height() - controls_->height());
    testNote("settings hidden at startup");
    settingsShortcut_->click();
    auto at = [this](int ms, std::function<void()> step) { QTimer::singleShot(ms, this, step); };
    at(500, [=] {
      testNote("compact stopped controls");
      controls_->grab().save(output + "/controls-stopped.png");
      settings_->grab().save(output + "/settings.png");
      tabs_->setCurrentIndex(1);
    });
    at(750, [=] {
      testNote("diagnostics tab");
      settings_->grab().save(output + "/diagnostics.png");
      obs_hotkey_trigger_routed_callback(hotkeys_[0], true);
    });
    at(3000, [=] { obs_frontend_recording_start(); });
    at(4000, [=] {
      testNote("active default theme");
      controls_->grab().save(output + "/controls-active.png");
      controlsDock->hide();
      settings_->close();
    });
    at(5000, [=] {
      testSave(0);
      testNote("hotkey with dock and window hidden");
    });
    at(6000, [=] {
      controlsDock->show();
      settingsShortcut_->click();
      testNote("dock shortcut while buffering");
    });
    at(7000, [=] { testTheme("com.obsproject.Yami.Acri"); });
    at(8000, [=] { testNote("active Acri theme"); });
    at(9000, [=] { testTheme("com.obsproject.Yami.Rachni"); });
    at(10000, [=] { testNote("active Rachni theme"); });
    at(11000, [=] { testTheme("com.obsproject.Yami.Light"); });
    at(12000, [=] { testNote("active Light theme"); });
    at(13000, [=] { obs_frontend_recording_stop(); });
    at(14000, [=] {
      saves_[1]->click();
      saves_[1]->click(); // Same-second collision, independently written.
    });
    at(14250, [=] {
      stopBuffer();
      settings_->close();
      settingsShortcut_->click();
      testNote("draining controls");
    });
    at(17500, [=] {
      testNote("stopped after draining");
      controlsDock->setFloating(false);
      main->addDockWidget(Qt::RightDockWidgetArea, controlsDock);
      controlsDock->show();
      controlsDock->raise();
    });
    at(18500, [=] {
      settings_->close();
      settingsShortcut_->click();
      testNote("docked settings shortcut");
      controls_->grab().save(output + "/controls-docked.png");
      settings_->close();
      testNote("replay dock layout saved");
      testFinish(output);
    });
  }
  void testCheck(bool passed, const char *description)
  {
    if (passed)
      checks_.append(description);
    else
      error(std::string("Settings test failed: ") + description);
  }
  bool testPersistedSettings(const BufferSettings &expected)
  {
    const char *json = config_get_string(obs_frontend_get_profile_config(), section, "Settings");
    auto *data = obs_data_create_from_json(json ? json : "{}");
    if (!data) return false;
    bool match = obs_data_get_double(data, "budget_gb") == expected.budgetGb &&
                 q(obs_data_get_string(data, "folder")) == expected.folder &&
                 obs_data_get_bool(data, "share_encoder") == expected.shareEncoder &&
                 obs_data_get_bool(data, "separate_folders") == expected.separateFolders;
    for (int i = 0; i < 3; ++i)
      match &= q(obs_data_get_string(data, ("preset_folder" + std::to_string(i)).c_str())) == expected.presetFolders[i];
    obs_data_release(data);
    return match;
  }
  void runSettingsTest(const QString &output)
  {
    auto at = [this](int ms, std::function<void()> step) { QTimer::singleShot(ms, this, step); };
    testCheck(!settings_->isVisible(), "window starts hidden");
    settingsAction_->trigger();
    testCheck(!settings_->isModal() && tabs_->count() == 2, "modeless window has two tabs");
    testCheck(!settingsButtons_->button(QDialogButtonBox::Apply)->isEnabled(), "Apply starts disabled without changes");
    settings_->resize(440, 300);
    tabs_->setCurrentIndex(1);
    budget_->setValue(0.5);
    folder_->setText(output + "/unapplied");
    separateEncoder_->setChecked(false);
    separateFolders_->setChecked(true);
    presetFolders_[0]->setText(output + "/unapplied preset");
    settingsAction_->trigger();
    testCheck(tabs_->currentIndex() == 1 && budget_->value() == 0.5 &&
                  settings_->parent()->findChildren<QDialog *>("multiReplaySettingsDialog").size() == 1,
              "repeated menu activation preserves tab and edits in one window");
    settingsShortcut_->click();
    settingsShortcut_->click();
    testCheck(settings_->isVisible() && tabs_->currentIndex() == 1 && budget_->value() == 0.5 &&
                  folder_->text() == output + "/unapplied" && !separateEncoder_->isChecked() &&
                  separateFolders_->isChecked() && presetFolders_[0]->text() == output + "/unapplied preset" &&
                  settings_->parent()->findChildren<QDialog *>("multiReplaySettingsDialog").size() == 1,
              "repeated dock shortcut preserves the menu window, tab and all pending edits");
    tabs_->setCurrentIndex(0);
    testCheck(budget_->value() == 0.5 && !separateEncoder_->isChecked(), "switching tabs preserves pending settings");
    seconds_[0]->setValue(3);
    seconds_[0]->setValue(2);
    names_[0]->setText("Saved preset");
    QMetaObject::invokeMethod(names_[0], "editingFinished", Qt::DirectConnection);
    obs_frontend_save();
    auto *bindings = obs_data_array_create();
    auto *binding = obs_data_create();
    obs_data_set_string(binding, "key", "OBS_KEY_F24");
    obs_data_array_push_back(bindings, binding);
    obs_hotkey_load(hotkeys_[0], bindings);
    obs_data_release(binding);
    obs_data_array_release(bindings);
    at(500, [=] {
      testCheck(testPersistedSettings({0.25, output, false}) && budget_->value() == 0.5,
                "preset, hotkey and frontend saves exclude pending settings");
      auto *data = obs_data_create_from_json(config_get_string(obs_frontend_get_profile_config(), section, "Settings"));
      auto *savedBindings = obs_data_get_array(data, "hotkey0");
      testCheck(obs_data_array_count(savedBindings) == 1 && q(obs_data_get_string(data, "name0")) == "Saved preset",
                "preset names and hotkey bindings still save immediately");
      obs_data_array_release(savedBindings);
      obs_data_release(data);
      settingsButtons_->button(QDialogButtonBox::Cancel)->click();
      testCheck(!settings_->isVisible() && draftSettings() == applied_, "Cancel discards edits");
      settingsShortcut_->click();
      testCheck(settings_->isVisible() && tabs_->currentIndex() == 0, "dock shortcut reopening selects Settings");
      budget_->setValue(0.5);
      settingsButtons_->button(QDialogButtonBox::Apply)->click();
      testCheck(settings_->isVisible() && testPersistedSettings({0.5, output, false}) &&
                    !settingsButtons_->button(QDialogButtonBox::Apply)->isEnabled(),
                "Apply commits and leaves the window open");
      budget_->setValue(0.75);
      settingsButtons_->button(QDialogButtonBox::Cancel)->click();
      testCheck(applied_.budgetGb == 0.5 && budget_->value() == 0.5, "Cancel retains the last Apply");
      settingsAction_->trigger();
      budget_->setValue(0.75);
      settingsButtons_->button(QDialogButtonBox::Ok)->click();
      testCheck(!settings_->isVisible() && testPersistedSettings({0.75, output, false}), "OK commits and closes");
      settingsAction_->trigger();
      budget_->setValue(0.9);
      QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
      QApplication::sendEvent(settings_, &escape);
      testCheck(!settings_->isVisible() && budget_->value() == 0.75, "Escape discards edits");
      settingsAction_->trigger();
      budget_->setValue(0.9);
      settings_->close();
      testCheck(!settings_->isVisible() && budget_->value() == 0.75, "window close discards edits");
      settingsAction_->trigger();
      budget_->setValue(0.8);
      separateFolders_->setChecked(true);
      presetFolders_[0]->setText(output + "/unapplied preset");
      char *profilePath = obs_frontend_get_current_profile_path();
      const QString blockedTmp = q(profilePath) + "/basic.ini.tmp";
      bfree(profilePath);
      const bool blocked = QDir().mkdir(blockedTmp);
      testCheck(blocked, "prepare a real profile write failure");
      if (blocked) {
        settingsButtons_->button(QDialogButtonBox::Ok)->click();
        testCheck(settings_->isVisible() && settingsError_->isVisible() && budget_->value() == 0.8 &&
                      separateFolders_->isChecked() && !presetFolders_[0]->text().isEmpty() &&
                      applied_.budgetGb == 0.75 && testPersistedSettings({0.75, output, false}),
                  "failed persistence retains applied config, pending edits and open window");
        const auto expected = std::find(errors_.begin(), errors_.end(),
                                        QString("Could not persist replay settings to the current OBS profile"));
        if (expected != errors_.end()) errors_.erase(expected);
        testCheck(QDir().rmdir(blockedTmp), "remove write-failure obstruction");
      }
      separateFolders_->setChecked(false);
      presetFolders_[0]->clear();
      settingsButtons_->button(QDialogButtonBox::Apply)->click();
      testCheck(testPersistedSettings({0.8, output, false}) && !settingsError_->isVisible(),
                "failed save can be retried");
      budget_->setValue(0.9);
      separateFolders_->setChecked(true);
      presetFolders_[0]->setText(output + "/discard on profile switch");
      obs_frontend_set_current_profile("Replay Settings Test");
    });
    at(1750, [=] {
      testCheck(!settings_->isVisible() && applied_.budgetGb == 0.33 && draftSettings() == applied_,
                "profile switch closes the window, discards edits and loads the other profile");
      testCheck(applied_.shareEncoder && !separateEncoder_->isChecked(),
                "profile without encoder preference defaults to sharing with checkbox unchecked");
      settingsAction_->trigger();
      budget_->setValue(0.25);
      separateEncoder_->setChecked(true);
      settingsButtons_->button(QDialogButtonBox::Apply)->click();
      testCheck(!applied_.shareEncoder && separateEncoder_->isChecked() && testPersistedSettings(applied_),
                "checked separate encoder commits independent mode with the existing profile key");
      budget_->setValue(0.5);
      folder_->setText(output + "/unapplied");
      separateEncoder_->setChecked(false);
      separateFolders_->setChecked(true);
      presetFolders_[0]->setText(output + "/pending while active");
      obs_hotkey_trigger_routed_callback(hotkeys_[0], true);
    });
    at(3500, [=] {
      const auto status = capture_.status();
      testCheck(status.collecting && !status.shared && sessionFolder_ == filePath(output) && applied_.budgetGb == 0.25,
                "hotkey starts buffering with applied folder, budget and sharing");
      testCheck(!budget_->isEnabled() && !folder_->isEnabled() && !browse_->isEnabled() &&
                    !separateEncoder_->isEnabled() && !settingsButtons_->button(QDialogButtonBox::Apply)->isEnabled() &&
                    !settingsButtons_->button(QDialogButtonBox::Ok)->isEnabled() && !separateFolders_->isEnabled() &&
                    !presetFolders_[0]->isEnabled() && !presetBrowse_[0]->isEnabled() && !applySettings(),
                "buffering locks fields and prevents commits");
      tabs_->setCurrentIndex(1);
      settingsShortcut_->click();
      testCheck(settingsShortcut_->isEnabled() && settings_->isVisible() && tabs_->currentIndex() == 1 &&
                    budget_->value() == 0.5 && !(draftSettings() == applied_),
                "dock shortcut stays available while buffering and retains pending edits");
      testCheck(tabs_->isEnabled() && details_->text().contains("0.25 GB") && status.available > 0,
                "Diagnostics stays accessible and reports applied settings");
      testNote("settings while buffering");
      testSave(0);
    });
    at(4500, [=] {
      saveReplay(0, clockNow());
      stopBuffer();
      settingsShortcut_->click();
      testCheck(settingsShortcut_->isEnabled() && settings_->isVisible() && tabs_->currentIndex() == 1 &&
                    budget_->value() == 0.5 && !(draftSettings() == applied_),
                "dock shortcut stays available while draining and retains pending edits");
      testCheck(capture_.status().draining && !settingsButtons_->button(QDialogButtonBox::Apply)->isEnabled() &&
                    !applySettings(),
                "draining prevents commits");
    });
    at(7500, [=] {
      testCheck(!capture_.status().draining && budget_->value() == 0.5 && !separateEncoder_->isChecked() &&
                    settingsButtons_->button(QDialogButtonBox::Apply)->isEnabled(),
                "pending edits survive buffering and unlock after draining");
      settingsButtons_->button(QDialogButtonBox::Cancel)->click();
      startBuffer();
      settingsShortcut_->click();
      testCheck(settings_->isVisible() && tabs_->currentIndex() == 0 && capture_.status().collecting,
                "dock shortcut opens a closed window while buffering");
      testCheck(settingsButtons_->button(QDialogButtonBox::Ok)->isEnabled(),
                "clean OK remains available while buffering");
      settingsButtons_->button(QDialogButtonBox::Ok)->click();
      testCheck(!settings_->isVisible() && capture_.status().collecting, "closing the window leaves buffering active");
    });
    at(9000, [=] { stopBuffer(); });
    at(12000, [=] {
      settingsAction_->trigger();
      budget_->setValue(0.95);
      separateFolders_->setChecked(true);
      presetFolders_[0]->setText(output + "/unapplied shutdown");
      testNote("pending settings before shutdown");
      testFinish(output);
    });
  }
  void testFolderError(int preset, const QString &folder)
  {
    const auto found = std::find_if(errors_.begin(), errors_.end(), [=](const QString &message) {
      return message.startsWith(QString("Preset %1 — ").arg(preset + 1)) && message.contains(folder);
    });
    testCheck(found != errors_.end(), "failed save identifies its preset and destination");
    if (found != errors_.end()) {
      expectedFolderErrors_.append(*found);
      errors_.erase(found);
    }
    testCheck(capture_.status().collecting, "a bad destination leaves buffering active");
  }
  void runFolderTest(const QString &output)
  {
    auto at = [this](int ms, std::function<void()> step) { QTimer::singleShot(ms, this, step); };
    const auto obsFolder = output + "/../recordings";
    const QString shared = output + "/Shared", a = output + "/Preset 1 α", b = output + "/Preset 2 β",
                  c = output + "/Preset 3 γ";
    const QString missing = output + "/Missing", disconnected = output + "/Disconnect me",
                  readOnly = output + "/Read only";
    for (const auto &path : {shared, a, b, c, disconnected, readOnly})
      testCheck(QDir().mkpath(path), "create scratch replay destination");
    testCheck(QFile::setPermissions(readOnly, QFileDevice::ReadOwner | QFileDevice::ExeOwner),
              "make an unwritable scratch folder");
    settingsAction_->trigger();
    for (int i = 0; i < 3; ++i) seconds_[i]->setValue(2);
    names_[0]->setText("Short α");
    names_[1]->setText("Medium β");
    names_[2]->setText("Long γ");
    separateFolders_->setChecked(true);
    presetFolders_[0]->setText(a);
    separateFolders_->setChecked(false);
    testCheck(presetFolders_[0]->text() == a && folder_->isEnabled(), "turning separate folders off retains paths");
    separateFolders_->setChecked(true);
    testCheck(presetFolders_[0]->text() == a && !folder_->isEnabled() && !browse_->isEnabled() &&
                  presetFolders_[0]->isEnabled(),
              "turning separate folders on restores paths and disables shared controls");
    settingsButtons_->button(QDialogButtonBox::Cancel)->click();
    testCheck(!separateFolders_->isChecked() && presetFolders_[0]->text().isEmpty(),
              "Cancel discards folder mode and path edits");
    settingsAction_->trigger();
    for (int phase = 0; phase < 9; ++phase) {
      const int base = 250 + phase * 6000;
      at(base, [=] {
        separateFolders_->setChecked(phase >= 2);
        folder_->setText(phase == 0 ? "" : shared);
        presetFolders_[0]->setText(phase >= 3 ? a : "");
        presetFolders_[1]->setText(phase >= 4 ? b : "");
        presetFolders_[2]->setText(phase >= 5 ? c : "");
        if (phase == 6) presetFolders_[0]->setText(missing);
        if (phase == 7) presetFolders_[2]->setText(disconnected);
        if (phase == 8) presetFolders_[2]->setText(readOnly);
        testCheck(applySettings() && testPersistedSettings(applied_), "Apply persists folder mode and all paths");
        if (phase == 5) settings_->grab().save(output + "/preset-folders-settings.png");
        startBuffer();
        testCheck(capture_.status().collecting, "buffer starts for the selected folder configuration");
        std::array<QString, 3> expected = {phase == 1 ? shared : obsFolder, phase == 1 ? shared : obsFolder,
                                           phase == 1 ? shared : obsFolder};
        if (phase >= 3) expected[0] = a;
        if (phase >= 4) expected[1] = b;
        if (phase >= 5) expected[2] = c;
        if (phase == 6) expected[0] = missing;
        if (phase == 7) expected[2] = disconnected;
        if (phase == 8) expected[2] = readOnly;
        bool routed = true;
        for (int i = 0; i < 3; ++i)
          routed &= sessionFolders_[i].lexically_normal() == filePath(expected[i]).lexically_normal();
        testCheck(routed, "each session resolves independent OBS and custom destinations");
        testCheck(!separateFolders_->isEnabled() && !presetFolders_[0]->isEnabled() && !presetBrowse_[2]->isEnabled(),
                  "folder controls lock during buffering");
        if (phase == 5) {
          tabs_->setCurrentIndex(1);
          testCheck(encoder_->text().contains(a) && encoder_->text().contains(b) && encoder_->text().contains(c),
                    "Diagnostics lists all three active destinations");
        }
        testNote(qPrintable(QString("folder phase %1").arg(phase)));
      });
      at(base + 1700, [=] {
        if (phase == 7) testCheck(QDir().rmdir(disconnected), "disconnect one folder while buffering");
        if (phase == 5) {
          presetFolders_[0]->setText(output + "/Unapplied");
          testCheck(!applySettings() && sessionFolders_[0] == filePath(a),
                    "draft paths cannot redirect an active session");
          settings_->grab().save(output + "/preset-folders-diagnostics.png");
        }
        saves_[0]->click();
        testSave(1);
        saves_[2]->click();
      });
      at(base + 5000, [=] {
        if (phase == 6 || phase == 7) {
          refresh();
          testFolderError(phase == 6 ? 0 : 2, phase == 6 ? missing : disconnected);
          testCheck(error_.contains(phase == 6 ? missing : disconnected),
                    "other presets succeeding do not clear a destination error");
        }
        if (phase == 8) {
          refresh();
          testFolderError(2, readOnly);
        }
        stopBuffer();
      });
    }
    at(56000, [=] {
      testCheck(!capture_.status().draining && capture_.status().memory == 0,
                "multi-folder saves drain and release replay memory");
      testCheck(
          QFile::setPermissions(readOnly, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner),
          "restore scratch folder permissions");
      separateFolders_->setChecked(false);
      folder_->setText(missing);
      testCheck(applySettings(), "apply missing shared folder while stopped");
      startBuffer();
      testCheck(!capture_.status().collecting && error_.contains("existing directory"),
                "shared mode retains startup validation");
      const auto expected = std::find(errors_.begin(), errors_.end(), error_);
      if (expected != errors_.end()) errors_.erase(expected);
      error_.clear();
      folder_->setText(shared);
      separateFolders_->setChecked(true);
      presetFolders_[0]->setText(a);
      presetFolders_[1]->setText(b);
      presetFolders_[2]->setText(c);
      settingsButtons_->button(QDialogButtonBox::Ok)->click();
      testCheck(!settings_->isVisible() && testPersistedSettings(applied_), "OK persists preset folders for restart");
      testFinish(output);
    });
  }
  void testSave(int preset) { obs_hotkey_trigger_routed_callback(hotkeys_[preset + 2], true); }
  void testFinish(const QString &output)
  {
    auto status = capture_.status();
    QJsonObject report;
    report["memory_after_stop"] = double(status.memory);
    report["results"] = int(status.results.size());
    report["collecting"] = status.collecting;
    report["draining"] = status.draining;
    report["encoder"] = encoder_->text();
    report["steps"] = steps_;
    report["checks"] = checks_;
    report["expected_folder_errors"] = expectedFolderErrors_;
    QJsonArray saved, errors;
    for (auto &result : testResults_) {
      QJsonObject value;
      value["path"] = pathText(result.path);
      value["error"] = q(result.error);
      value["partial"] = q(result.partial);
      value["requested"] = double(result.requested) / 1e6;
      value["actual"] = double(result.actual) / 1e6;
      value["end_delta_ms"] = double(result.mediaEnd - result.endpoint) / 1e3;
      saved.append(value);
    }
    for (auto &message : errors_) errors.append(message);
    report["saved"] = saved;
    report["errors"] = errors;
    report["frontend_active_at_close"] = obs_frontend_recording_active() || obs_frontend_streaming_active() ||
                                         obs_frontend_replay_buffer_active() || obs_frontend_virtualcam_active();
    QFile file(output + "/report.json");
    if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(report).toJson());
    closeObs();
  }
  // Drives the real frontend recording against a shared-encoder replay session.
  // The OBS log shows how many encoders were created. REPLAY_TEST_SHARED selects:
  //   reset - OBS settings are applied while buffering, which recreates the frontend's encoders
  //   long  - ten minutes of media with a recording pause, then full 60/240/600-second saves
  //   other - both start orders, pause/resume and recording stop
  void runSharedTest(const QString &output)
  {
    const auto mode = qEnvironmentVariable("REPLAY_TEST_SHARED");
    auto at = [this](int ms, std::function<void()> step) { QTimer::singleShot(ms, this, step); };
    if (mode == "reset") {
      at(500, [=] {
        startBuffer();
        testNote("replay started first");
      });
      at(4000, [=] {
        // Apply a recording setting through OBS's own settings dialog.
        QTimer::singleShot(1500, qApp, [] {
          auto *dialog = QApplication::activeModalWidget();
          auto *name = dialog ? dialog->findChild<QLineEdit *>("advOutTrack1Name") : nullptr;
          auto *buttons = dialog ? dialog->findChild<QDialogButtonBox *>("buttonBox") : nullptr;
          blog(LOG_INFO, "[multi-replay] test: settings dialog %s", name && buttons ? "found" : "NOT found");
          if (name) name->setText(name->text() + " edited");
          if (buttons)
            buttons->button(QDialogButtonBox::Ok)->click();
          else if (dialog)
            dialog->close();
        });
        QMetaObject::invokeMethod(static_cast<QObject *>(obs_frontend_get_main_window()),
                                  "on_action_Settings_triggered", Qt::QueuedConnection);
      });
      at(9000, [=] {
        testNote("after OBS settings were applied");
        testSave(0);
      });
      at(10000, [=] { obs_frontend_recording_start(); });
      at(14500, [=] {
        testNote("recording on the frontend's new encoder");
        testSave(1);
      });
      at(15500, [=] { obs_frontend_recording_stop(); });
      at(17500, [=] { stopBuffer(); });
      at(20000, [=] {
        startBuffer();
        testNote("replay restarted");
      });
      at(25000, [=] {
        testNote("sharing the new encoder");
        testSave(0);
      });
      at(26500, [=] { stopBuffer(); });
      at(29000, [=] { testFinish(output); });
      return;
    }
    if (mode == "long") {
      at(500, [=] {
        startBuffer();
        testNote("replay started first");
      });
      at(20000, [=] { obs_frontend_recording_start(); });
      at(200000, [=] {
        testNote("before pause");
        obs_frontend_recording_pause(true);
      });
      at(220000, [=] {
        testNote("paused 20 s");
        obs_frontend_recording_pause(false);
      });
      for (int ms = 60000; ms < 660000; ms += 60000) at(ms + 1000, [=] { testNote("running"); });
      at(665000, [=] {
        testNote("before full saves");
        for (int preset : {0, 1, 2}) testSave(preset);
      });
      at(668000, [this, output] { controls_->grab().save(output + "/dock.png"); });
      at(670000, [=] { obs_frontend_recording_stop(); });
      at(673000, [=] {
        testNote("recording stopped");
        stopBuffer();
      });
      at(680000, [=] { testFinish(output); });
      return;
    }
    at(500, [=] {
      startBuffer();
      testNote("replay started first");
    });
    at(4000, [=] { testSave(0); });
    at(4500, [=] { obs_frontend_recording_start(); });
    at(10000, [=] {
      testNote("recording joined");
      testSave(1);
    });
    at(11000, [=] { obs_frontend_recording_pause(true); });
    at(17500, [=] {
      testNote("recording paused 6.5 s");
      testSave(0);
    });
    at(18000, [=] { obs_frontend_recording_pause(false); });
    at(21000, [=] {
      testNote("recording resumed");
      testSave(2);
    });
    at(22000, [=] { obs_frontend_recording_stop(); });
    at(25000, [=] {
      testNote("recording stopped, replay continues");
      testSave(0);
    });
    at(26000, [this, output] { controls_->grab().save(output + "/dock.png"); });
    at(26500, [=] { stopBuffer(); });
    at(29000, [=] {
      testNote("replay stopped");
      obs_frontend_recording_start();
    });
    at(32000, [=] {
      startBuffer();
      testNote("replay joined a running recording");
    });
    at(38000, [=] { testSave(0); });
    at(39500, [=] { stopBuffer(); });
    at(41500, [=] {
      testNote("replay stopped, recording must continue");
      obs_frontend_recording_stop();
    });
    at(44500, [=] { testFinish(output); });
  }
  QJsonArray steps_, checks_, expectedFolderErrors_;
  std::vector<Result> testResults_;
#endif
  void startBuffer()
  {
    if (closed_ || profilePending_) return;
    try {
      auto status = capture_.status();
      if (status.collecting) return;
      if (status.draining) throw std::runtime_error("Accepted saves are draining; wait before starting");
      if (obs_frontend_replay_buffer_active())
        throw std::runtime_error(
            "OBS's built-in replay buffer is active; stop it manually before starting this buffer");
      // A previous session may still hold encoders the frontend has since replaced.
      capture_.releaseIdleOutput();
      auto encoders = recordingEncoders(applied_.shareEncoder);
      Options options;
      options.budget = uint64_t(applied_.budgetGb * 1000000000);
      options.shared = encoders.shared;
      options.trackNames = encoders.trackNames;
      options.filenames = encoders.filenames;
      std::array<std::filesystem::path, 3> folders;
      for (int i = 0; i < 3; ++i) {
        const auto &configured = applied_.separateFolders ? applied_.presetFolders[i] : applied_.folder;
        folders[i] = configured.trimmed().isEmpty() ? encoders.folder : filePath(configured);
      }
      options.folder = folders[0];
      if (!applied_.separateFolders && !std::filesystem::is_directory(options.folder))
        throw std::runtime_error("Replay destination must be an existing directory");
      int longest = 0;
      for (auto *spin : seconds_) longest = std::max(longest, spin->value());
      options.longest = Time(longest) * 1000000;
      saveSettings();
      capture_.start(options, encoders.video, encoders.audio);
      fingerprint_ = encoders.fingerprint;
      videoInfo_ = encoders.videoInfo;
      sessionFolder_ = options.folder;
      sessionFolders_ = std::move(folders);
      sharedVideo_ = encoders.shared ? encoders.video : nullptr;
      shareLost_ = false;
      if (encoders.shared && obs_frontend_recording_paused()) capture_.notePause(true);
      QString destinations;
      if (applied_.separateFolders) {
        destinations = "\nDestinations:";
        for (int i = 0; i < 3; ++i)
          destinations +=
              QString("\nPreset %1 — %2: %3").arg(i + 1).arg(names_[i]->text()).arg(pathText(sessionFolders_[i]));
      } else {
        destinations = "\nDestination: " + pathText(options.folder);
      }
      encoder_->setText(q(encoders.description) + destinations);
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
  void stopBuffer()
  {
    capture_.stop();
    refresh();
  }
  void saveReplay(int preset, Time endpoint)
  {
    try {
      const auto id = capture_.save(Time(seconds_[preset]->value()) * 1000000, endpoint, sessionFolders_[preset]);
      pendingPresets_[id] = {preset, QString("Preset %1 — %2: ").arg(preset + 1).arg(names_[preset]->text())};
      feedback_ = names_[preset]->text() + " accepted";
      feedbackDetail_ = "Saving the requested replay.";
      feedbackUntil_ = clockNow() + 5000000;
    } catch (const std::exception &e) {
      error((QString("Preset %1 — %2: ").arg(preset + 1).arg(names_[preset]->text()) + q(e.what())).toStdString(),
            true);
      saveErrorPreset_ = preset;
    }
    refresh();
  }
  void refresh()
  {
    if (closed_) return;
    capture_.poll();
    auto status = capture_.status();
    const bool busy = status.collecting || status.draining;
    start_->setText(status.draining ? "Draining…" : status.collecting ? "Stop Buffering" : "Start Buffering");
    start_->setEnabled(!status.draining && !profilePending_);
    const QString activeClass = status.collecting ? "state-active" : "";
    if (start_->property("class").toString() != activeClass) {
      start_->setProperty("class", activeClass);
      const auto stylesheet = start_->styleSheet();
      start_->setStyleSheet("/* refresh active state */");
      start_->setStyleSheet(stylesheet);
    }
    Time longest = 0;
    for (int i = 0; i < 3; ++i) {
      const Time requested = Time(seconds_[i]->value()) * 1000000;
      longest = std::max(longest, requested);
      seconds_[i]->setEnabled(!busy);
      names_[i]->setEnabled(!busy);
      const QString readiness = status.available >= requested ? "Full replay available"
                                : status.available > 0        ? "Partial replay available"
                                                              : "No playable replay available yet";
      saves_[i]->setToolTip(readiness + "\nRequested " + durationText(requested) + "; available " +
                            durationText(status.available));
      saves_[i]->setEnabled(status.collecting && status.available > 0);
    }
    updateSettingsState();
    if (!busy) sharedVideo_ = nullptr;
    details_->setText("Available " + durationText(status.available) + " / target " + durationText(longest) +
                      "\nReplay memory " + QString::number(status.memory / 1e9, 'f', 3) + " / " +
                      QString::number(applied_.budgetGb, 'f', 2) + " GB · queue " + QString::number(status.queued) +
                      (status.active.empty() ? "" : "\nWriting " + q(status.active)));
    if (++ticks_ % 8 == 0 && status.collecting) {
      try {
        if (sharedVideo_ && !shareLost_) {
          // Applying OBS settings recreates the frontend's encoders; this session keeps the old ones.
          auto *current = frontendRecordingVideo();
          if (current != sharedVideo_) {
            shareLost_ = true;
            error("OBS recreated its recording encoder. A recording started now would use a second encoder; restart "
                  "buffering to share again.");
          }
          if (current) obs_encoder_release(current);
        }
        // Shown on every check, so it must not hide the lost-sharing warning above.
        if (shareLost_)
          warning_ = "OBS recreated its recording encoder. A recording started now would use a second encoder; "
                     "restart buffering to share again.";
        else if (recordingFingerprint() != fingerprint_)
          warning_ = "Recording settings changed. This session retains its original encoding; restart buffering "
                     "to apply them.";
        obs_video_info current{};
        obs_get_video_info(&current);
        if (current.output_width != videoInfo_.output_width || current.output_height != videoInfo_.output_height ||
            current.output_format != videoInfo_.output_format || current.colorspace != videoInfo_.colorspace ||
            current.range != videoInfo_.range || current.fps_num != videoInfo_.fps_num ||
            current.fps_den != videoInfo_.fps_den) {
          capture_.stop();
          error("OBS video format changed; previous accepted saves are draining. Restart buffering manually.");
        }
      } catch (const std::exception &e) {
        error(e.what());
      }
    }
    for (const auto &saved : capture_.takeCompleted()) {
#ifdef REPLAY_INTEGRATION_TEST
      testResults_.push_back(saved);
#endif
      if (saved.id <= lastResultId_) continue;
      lastResultId_ = saved.id;
      const auto request = pendingPresets_.find(saved.id);
      const int presetIndex = request == pendingPresets_.end() ? -1 : request->second.index;
      const QString preset = request == pendingPresets_.end() ? "" : request->second.label;
      if (request != pendingPresets_.end()) pendingPresets_.erase(request);
      feedbackDetail_ = "Requested " + durationText(saved.requested) + ", actual " + durationText(saved.actual);
      if (!saved.partial.empty()) feedbackDetail_ += "\nPartial: " + q(saved.partial);
      if (!saved.error.empty()) {
        error((preset + q(saved.error)).toStdString(), true);
        saveErrorPreset_ = presetIndex;
      } else {
        // Another preset succeeding does not repair this preset's destination.
        if (saveError_ && (saveErrorPreset_ < 0 || saveErrorPreset_ == presetIndex)) {
          error_.clear();
          saveError_ = false;
          saveErrorPreset_ = -1;
        }
        feedback_ = "Saved " + pathText(saved.path.filename());
        feedbackDetail_ += "\n" + pathText(saved.path);
        feedbackUntil_ = clockNow() + 5000000;
      }
    }
    QString state = status.paused     ? "Paused with recording"
                    : status.draining ? "Draining accepted saves"
                                      : q(status.message.empty() ? "Stopped" : status.message);
    QString currentWarning = warning_;
    const bool captureError = !status.message.empty() && status.message != "Stopped" && status.message != "Starting" &&
                              status.message != "Buffering" && status.message != "Draining accepted saves";
    if (captureError) currentWarning = q(status.message) + (currentWarning.isEmpty() ? "" : "\n" + currentWarning);
    if (status.degraded && status.collecting)
      currentWarning += (currentWarning.isEmpty() ? "" : "\n") +
                        QString("Memory pressure shortened history; partial clips may be saved.");
    if (!error_.isEmpty())
      state = "Error: " + error_;
    else if (!currentWarning.isEmpty())
      state += " · " + currentWarning;
    else if (!feedback_.isEmpty() && clockNow() < feedbackUntil_)
      state = feedback_;
    state_->setText(state);
    if (!feedbackDetail_.isEmpty()) state_->setToolTip(state + "\n" + feedbackDetail_);
    notice_->setText((error_.isEmpty() ? "" : "Error: " + error_ + "\n") + currentWarning +
                     (feedback_.isEmpty() ? "" : "\n" + feedback_ + "\n" + feedbackDetail_));
    notice_->setVisible(!notice_->text().trimmed().isEmpty());
  }

private:
  void error(const std::string &message, bool saveError = false)
  {
    error_ = q(message);
    saveError_ = saveError;
    if (!saveError) saveErrorPreset_ = -1;
    blog(LOG_WARNING, "[multi-replay] %s", message.c_str());
    errors_.push_front(q(message));
    if (errors_.size() > 20) errors_.pop_back();
  }
  QPointer<QWidget> controls_;
  QPointer<QDialog> settings_;
  QPointer<QAction> settingsAction_;
  QTabWidget *tabs_;
  QDialogButtonBox *settingsButtons_;
  QLabel *settingsLock_, *settingsError_;
  BufferSettings applied_;
  QCheckBox *separateFolders_;
  QWidget *presetFoldersView_;
  std::array<QLineEdit *, 3> presetFolders_{};
  std::array<QPushButton *, 3> presetBrowse_{};
  std::array<QLabel *, 3> presetFolderLabels_{};
  QWidget *diagnostics_;
  Capture capture_;
  std::array<QLineEdit *, 3> names_{};
  std::array<QSpinBox *, 3> seconds_{};
  std::array<QPushButton *, 3> saves_{};
  std::array<obs_hotkey_id, 5> hotkeys_{};
  std::array<HotkeyContext, 5> contexts_{};
  QPushButton *start_, *browse_, *settingsShortcut_;
  ElidedLabel *state_;
  QLabel *details_, *encoder_, *notice_;
  QLineEdit *folder_;
  QDoubleSpinBox *budget_;
  QCheckBox *separateEncoder_;
  // Identity of the frontend encoder this session shares; compared, never dereferenced.
  const obs_encoder_t *sharedVideo_ = nullptr;
  bool shareLost_ = false;
  std::atomic<bool> bindingSavePending_{false};
  QTimer *timer_;
  std::string fingerprint_;
  QString error_, warning_, feedback_, feedbackDetail_;
  Time feedbackUntil_ = 0;
  uint64_t lastResultId_ = 0;
  std::deque<QString> errors_;
  obs_video_info videoInfo_{};
  std::filesystem::path sessionFolder_;
  std::array<std::filesystem::path, 3> sessionFolders_{};
  struct PendingPreset {
    int index;
    QString label;
  };
  std::unordered_map<uint64_t, PendingPreset> pendingPresets_;
  int ticks_ = 0;
  bool loading_ = false, closed_ = false, profilePending_ = false;
  bool saveError_ = false;
  int saveErrorPreset_ = -1;
};
QPointer<ReplayController> dock;
void frontendEvent(obs_frontend_event event, void *)
{
  if (!dock) return;
  switch (event) {
  case OBS_FRONTEND_EVENT_PROFILE_CHANGING: dock->profileChanging(); break;
  case OBS_FRONTEND_EVENT_PROFILE_CHANGED: dock->profileChanged(); break;
  case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING: dock->stopBuffer(); break;
  case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED: dock->builtInStarted(); break;
  case OBS_FRONTEND_EVENT_RECORDING_PAUSED: dock->recordingPaused(true); break;
  case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
  case OBS_FRONTEND_EVENT_RECORDING_STOPPED: dock->recordingPaused(false); break;
  case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN: dock->shutdown(); break;
  case OBS_FRONTEND_EVENT_EXIT: dock->shutdown(); break;
#ifdef REPLAY_INTEGRATION_TEST
  case OBS_FRONTEND_EVENT_FINISHED_LOADING: dock->runIntegrationTest(); break;
#endif
  default: break;
  }
}
void saveCallback(obs_data_t *, bool saving, void *)
{
  if (dock && saving) dock->saveSettings();
}
}
bool obs_module_load(void)
{
  if (obs_get_version() < MAKE_SEMANTIC_VERSION(32, 1, 0)) {
    blog(LOG_ERROR, "[multi-replay] OBS 32.1 or newer is required");
    return false;
  }
  registerReplayOutput();
  return true;
}
void obs_module_post_load(void)
{
  blog(LOG_INFO, "[multi-replay] Initializing dock");
#ifdef _WIN32
  char *writer = obs_module_file("multi-replay-writer.exe");
#else
  char *writer = obs_module_file("multi-replay-writer");
#endif
  if (!writer) {
    blog(LOG_ERROR, "[multi-replay] Writer helper missing from plugin data directory");
    return;
  }
  replay::setWriterPath(std::filesystem::u8path(writer));
  bfree(writer);
  auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
  auto *content = new ReplayController(main);
  if (!obs_frontend_add_dock_by_id(dockId, "Multi-Length Replay", content->controls())) {
    blog(LOG_ERROR, "[multi-replay] Cannot register replay dock");
    delete content;
    return;
  }
  if (!content->addSettingsMenu()) {
    blog(LOG_ERROR, "[multi-replay] Cannot register Tools settings action");
    content->shutdown();
    obs_frontend_remove_dock(dockId);
    delete content;
    return;
  }
  dock = content;
  obs_frontend_add_event_callback(frontendEvent, nullptr);
  obs_frontend_add_save_callback(saveCallback, nullptr);
  blog(LOG_INFO, "[multi-replay] Replay dock and Tools settings ready; manual startup, defaults 60/240/600 seconds");
}
void obs_module_unload(void)
{
  obs_frontend_remove_event_callback(frontendEvent, nullptr);
  obs_frontend_remove_save_callback(saveCallback, nullptr);
  if (dock) {
    dock->shutdown();
    obs_frontend_remove_dock(dockId);
    if (dock) delete dock.data();
    dock.clear();
  }
}
