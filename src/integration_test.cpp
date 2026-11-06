// UI test driver for isolated OBS runs. Built only with REPLAY_INTEGRATION_TEST=ON
// and never shipped: the packaging scripts refuse such builds. scripts/test-obs-ui.py
// picks a scenario through REPLAY_TEST_* variables; the driver then operates the
// real dock, settings window and OBS frontend on timers, writes report.json to
// REPLAY_TEST_OUTPUT and closes OBS.
#ifndef REPLAY_INTEGRATION_TEST
#error "integration_test.cpp is only built with REPLAY_INTEGRATION_TEST=ON"
#endif
#include "identifiers.hpp"
#include "replay_controller.hpp"
#include "replay_dock.hpp"
#include "settings_dialog.hpp"
#include "ui_util.hpp"
#include <obs-frontend-api.h>
#include <util/bmem.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <functional>
#include <vector>

namespace replay {
class IntegrationDriver : public QObject {
public:
  explicit IntegrationDriver(ReplayController &controller)
      : QObject(&controller), ctl(controller), dock(controller.dock_), dialog(controller.dialog_)
  {
  }
  void run()
  {
    auto output = qEnvironmentVariable("REPLAY_TEST_OUTPUT");
    if (output.isEmpty()) return;
    if (qEnvironmentVariable("REPLAY_TEST_FOLDERS") == "restore") {
      QTimer::singleShot(500, this, [this, output] {
        const auto base = qEnvironmentVariable("REPLAY_TEST_FOLDER_BASE");
        testCheck(ctl.applied_.separateFolders && ctl.applied_.presetFolders.size() == 3 &&
                      q(ctl.applied_.presetFolders[0]) == base + "/Preset 1 α" &&
                      q(ctl.applied_.presetFolders[1]) == base + "/Preset 2 β" &&
                      q(ctl.applied_.presetFolders[2]) == base + "/Preset 3 γ",
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
    if (qEnvironmentVariableIsSet("REPLAY_TEST_PRESETS")) {
      runPresetTest(output, qEnvironmentVariable("REPLAY_TEST_PRESETS") == "restore");
      return;
    }
    // The other scenarios were written for three presets; new profiles start with two.
    while (dock->presetCount() < 3) ctl.addPreset();
    dialog->separateFolders->setChecked(false);
    for (auto *field : dialog->presetFolders) field->clear();
    dialog->folder->setText(output);
    dock->rows[0].seconds->setValue(2);
    dock->rows[1].seconds->setValue(4);
    dock->rows[2].seconds->setValue(6);
    dialog->budget->setValue(0.25);
    dialog->separateEncoder->setChecked(!qEnvironmentVariableIsSet("REPLAY_TEST_SHARED"));
    if (qEnvironmentVariable("REPLAY_TEST_SHARED") == "long") {
      dock->rows[0].seconds->setValue(60);
      dock->rows[1].seconds->setValue(240);
      dock->rows[2].seconds->setValue(600);
      dialog->budget->setValue(16);
    }
    ctl.applySettings();
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
    if (ctl.applied_.shareEncoder) {
      runSharedTest(output);
      return;
    }
    QTimer::singleShot(500, this, [this] { ctl.startBuffer(); });
    QTimer::singleShot(
        4000, this, [this] { obs_hotkey_trigger_routed_callback(ctl.hotkeys_->id(HotkeyAction::SavePreset1), true); });
    QTimer::singleShot(6500, this, [this] {
      for (int key : {0, 1, 0, 2}) obs_hotkey_trigger_routed_callback(ctl.hotkeys_->id(saveAction(key)), true);
    });
    QTimer::singleShot(8500, this, [this, output] { dock->grab().save(output + "/dock.png"); });
    QTimer::singleShot(10000, this, [this] { ctl.stopBuffer(); });
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
    auto status = ctl.capture_.status();
    QJsonObject value;
    value["step"] = step;
    value["collecting"] = status.collecting;
    value["shared"] = status.shared;
    value["paused"] = status.paused;
    value["available"] = double(status.available) / 1e6;
    value["memory_mb"] = double(status.memory) / 1e6;
    value["recording"] = obs_frontend_recording_active();
    value["state"] = dock->state->fullText();
    value["notice"] = dialog->notice->text();
    value["button_class"] = dock->start->property("class").toString();
    value["controls_visible"] = dock->isVisible();
    value["settings_visible"] = dialog->isVisible();
    value["settings_shortcut_visible"] = dock->settingsShortcut->isVisible();
    value["settings_shortcut_enabled"] = dock->settingsShortcut->isEnabled();
    value["settings_tab"] = dialog->tabs->currentIndex();
    value["settings_tabs"] = dialog->tabs->count();
    value["settings_dirty"] = !(ctl.draftSettings() == ctl.applied_);
    value["apply_enabled"] = dialog->settingsButtons->button(QDialogButtonBox::Apply)->isEnabled();
    value["applied_budget"] = ctl.applied_.budgetGb;
    value["applied_folder"] = q(ctl.applied_.folder);
    value["applied_share_encoder"] = ctl.applied_.shareEncoder;
    value["separate_encoder_checked"] = dialog->separateEncoder->isChecked();
    value["separate_folders"] = ctl.applied_.separateFolders;
    QJsonArray destinations;
    for (const auto &path : ctl.sessionFolders_) destinations.append(pathText(path));
    value["destinations"] = destinations;
    value["settings_height"] = dialog->height();
    value["settings_minimum_height"] = dialog->minimumSizeHint().height();
    value["diagnostics_fit"] = dialog->details->height() >= dialog->details->heightForWidth(dialog->details->width()) &&
                               dialog->encoder->height() >= dialog->encoder->heightForWidth(dialog->encoder->width());
    value["settings_locked"] =
        !dialog->budget->isEnabled() && !dialog->folder->isEnabled() && !dialog->separateEncoder->isEnabled();
    value["width"] = dock->width();
    value["height"] = dock->height();
    value["minimum_width"] = dock->minimumSizeHint().width();
    value["minimum_height"] = dock->minimumSizeHint().height();
    QJsonArray presetSeconds;
    for (const auto &row : dock->rows) presetSeconds.append(row.seconds->value());
    value["preset_seconds"] = presetSeconds;
    value["preset_count"] = dock->presetCount();
    value["save_tooltip"] = dock->rows[0].save->toolTip();
    value["notice_tooltip"] = dock->state->toolTip();
    value["last_result_id"] = double(ctl.lastResultId_);
    bool contained = dock->rect().contains(dock->start->geometry()) && dock->rect().contains(dock->state->geometry()) &&
                     dock->rect().contains(dock->settingsShortcut->geometry()) &&
                     dock->rect().contains(dock->add->geometry());
    for (const auto &row : dock->rows)
      contained &= dock->rect().contains(row.name->geometry()) && dock->rect().contains(row.seconds->geometry()) &&
                   dock->rect().contains(row.save->geometry()) && dock->rect().contains(row.remove->geometry());
    value["controls_contained"] = contained;
    auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
    auto *controlsDock = main->findChild<QDockWidget *>(ids::dock);
    value["replay_dock_registered"] = controlsDock != nullptr;
    value["settings_dock_registered"] = main->findChild<QDockWidget *>("obs-multi-replay-settings-dock") != nullptr;
    value["settings_action_registered"] =
        ctl.settingsAction_ && main->findChildren<QAction *>("multiReplaySettingsAction").size() == 1;
    value["settings_windows"] = main->findChildren<QDialog *>("multiReplaySettingsDialog").size();
    value["controls_docked_right"] =
        controlsDock && !controlsDock->isFloating() && main->dockWidgetArea(controlsDock) == Qt::RightDockWidgetArea;
    const auto color = dock->start->grab().toImage().pixelColor(dock->start->width() / 2, 5);
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
      if (index < 0 || !buttons) ctl.error("UI test could not select theme " + variant.toStdString());
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
    auto *controlsDock = main->findChild<QDockWidget *>(ids::dock);
    if (!controlsDock || !ctl.settingsAction_) {
      ctl.error("UI test could not find replay dock and settings action");
      testFinish(output);
      return;
    }
    controlsDock->setFloating(true);
    controlsDock->show();
    controlsDock->resize(320, 300 + controlsDock->height() - dock->height());
    testNote("settings hidden at startup");
    dock->settingsShortcut->click();
    at(500, [=] {
      testNote("compact stopped controls");
      dock->grab().save(output + "/controls-stopped.png");
      dialog->grab().save(output + "/settings.png");
      dialog->tabs->setCurrentIndex(1);
    });
    at(750, [=] {
      testNote("diagnostics tab");
      dialog->grab().save(output + "/diagnostics.png");
      obs_hotkey_trigger_routed_callback(ctl.hotkeys_->id(HotkeyAction::Start), true);
    });
    at(3000, [=] { obs_frontend_recording_start(); });
    at(4000, [=] {
      testNote("active default theme");
      dock->grab().save(output + "/controls-active.png");
      controlsDock->hide();
      dialog->close();
    });
    at(5000, [=] {
      testSave(0);
      testNote("hotkey with dock and window hidden");
    });
    at(6000, [=] {
      controlsDock->show();
      dock->settingsShortcut->click();
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
      dock->rows[1].save->click();
      dock->rows[1].save->click(); // Same-second collision, independently written.
    });
    at(14250, [=] {
      ctl.stopBuffer();
      dialog->close();
      dock->settingsShortcut->click();
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
      dialog->close();
      dock->settingsShortcut->click();
      testNote("docked settings shortcut");
      dock->grab().save(output + "/controls-docked.png");
      dialog->close();
      testNote("replay dock layout saved");
      testFinish(output);
    });
  }
  void testCheck(bool passed, const char *description)
  {
    if (passed)
      checks_.append(description);
    else
      ctl.error(std::string("Settings test failed: ") + description);
  }
  // `expected` may omit preset folders; blank ones are filled in for each preset.
  bool testPersistedSettings(ReplaySettings expected)
  {
    expected.presetFolders.resize(size_t(dock->presetCount()));
    auto *data = prefs::loadFromProfile(obs_frontend_get_profile_config());
    const bool match = prefs::readSettings(data) == expected;
    obs_data_release(data);
    return match;
  }
  void runSettingsTest(const QString &output)
  {
    testCheck(!dialog->isVisible(), "window starts hidden");
    ctl.settingsAction_->trigger();
    testCheck(!dialog->isModal() && dialog->tabs->count() == 2, "modeless window has two tabs");
    testCheck(!dialog->settingsButtons->button(QDialogButtonBox::Apply)->isEnabled(),
              "Apply starts disabled without changes");
    dialog->resize(440, 300);
    dialog->tabs->setCurrentIndex(1);
    dialog->budget->setValue(0.5);
    dialog->folder->setText(output + "/unapplied");
    dialog->separateEncoder->setChecked(false);
    dialog->separateFolders->setChecked(true);
    dialog->presetFolders[0]->setText(output + "/unapplied preset");
    ctl.settingsAction_->trigger();
    testCheck(dialog->tabs->currentIndex() == 1 && dialog->budget->value() == 0.5 &&
                  dialog->parent()->findChildren<QDialog *>("multiReplaySettingsDialog").size() == 1,
              "repeated menu activation preserves tab and edits in one window");
    dock->settingsShortcut->click();
    dock->settingsShortcut->click();
    testCheck(dialog->isVisible() && dialog->tabs->currentIndex() == 1 && dialog->budget->value() == 0.5 &&
                  dialog->folder->text() == output + "/unapplied" && !dialog->separateEncoder->isChecked() &&
                  dialog->separateFolders->isChecked() &&
                  dialog->presetFolders[0]->text() == output + "/unapplied preset" &&
                  dialog->parent()->findChildren<QDialog *>("multiReplaySettingsDialog").size() == 1,
              "repeated dock shortcut preserves the menu window, tab and all pending edits");
    dialog->tabs->setCurrentIndex(0);
    testCheck(dialog->budget->value() == 0.5 && !dialog->separateEncoder->isChecked(),
              "switching tabs preserves pending settings");
    dock->rows[0].seconds->setValue(3);
    dock->rows[0].seconds->setValue(2);
    dock->rows[0].name->setText("Saved preset");
    QMetaObject::invokeMethod(dock->rows[0].name, "editingFinished", Qt::DirectConnection);
    obs_frontend_save();
    auto *bindings = obs_data_array_create();
    auto *binding = obs_data_create();
    obs_data_set_string(binding, "key", "OBS_KEY_F24");
    obs_data_array_push_back(bindings, binding);
    obs_hotkey_load(ctl.hotkeys_->id(HotkeyAction::Start), bindings);
    obs_data_release(binding);
    obs_data_array_release(bindings);
    at(500, [=] {
      testCheck(testPersistedSettings({0.25, utf8(output), false}) && dialog->budget->value() == 0.5,
                "preset, hotkey and frontend saves exclude pending settings");
      auto *data = prefs::loadFromProfile(obs_frontend_get_profile_config());
      auto *savedBindings = obs_data_get_array(data, "hotkey0");
      testCheck(obs_data_array_count(savedBindings) == 1 && q(obs_data_get_string(data, "name0")) == "Saved preset",
                "preset names and hotkey bindings still save immediately");
      obs_data_array_release(savedBindings);
      obs_data_release(data);
      dialog->settingsButtons->button(QDialogButtonBox::Cancel)->click();
      testCheck(!dialog->isVisible() && ctl.draftSettings() == ctl.applied_, "Cancel discards edits");
      dock->settingsShortcut->click();
      testCheck(dialog->isVisible() && dialog->tabs->currentIndex() == 0, "dock shortcut reopening selects Settings");
      dialog->budget->setValue(0.5);
      dialog->settingsButtons->button(QDialogButtonBox::Apply)->click();
      testCheck(dialog->isVisible() && testPersistedSettings({0.5, utf8(output), false}) &&
                    !dialog->settingsButtons->button(QDialogButtonBox::Apply)->isEnabled(),
                "Apply commits and leaves the window open");
      dialog->budget->setValue(0.75);
      dialog->settingsButtons->button(QDialogButtonBox::Cancel)->click();
      testCheck(ctl.applied_.budgetGb == 0.5 && dialog->budget->value() == 0.5, "Cancel retains the last Apply");
      ctl.settingsAction_->trigger();
      dialog->budget->setValue(0.75);
      dialog->settingsButtons->button(QDialogButtonBox::Ok)->click();
      testCheck(!dialog->isVisible() && testPersistedSettings({0.75, utf8(output), false}), "OK commits and closes");
      ctl.settingsAction_->trigger();
      dialog->budget->setValue(0.9);
      QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
      QApplication::sendEvent(dialog, &escape);
      testCheck(!dialog->isVisible() && dialog->budget->value() == 0.75, "Escape discards edits");
      ctl.settingsAction_->trigger();
      dialog->budget->setValue(0.9);
      dialog->close();
      testCheck(!dialog->isVisible() && dialog->budget->value() == 0.75, "window close discards edits");
      ctl.settingsAction_->trigger();
      dialog->budget->setValue(0.8);
      dialog->separateFolders->setChecked(true);
      dialog->presetFolders[0]->setText(output + "/unapplied preset");
      char *profilePath = obs_frontend_get_current_profile_path();
      const QString blockedTmp = q(profilePath) + "/basic.ini.tmp";
      bfree(profilePath);
      const bool blocked = QDir().mkdir(blockedTmp);
      testCheck(blocked, "prepare a real profile write failure");
      if (blocked) {
        dialog->settingsButtons->button(QDialogButtonBox::Ok)->click();
        testCheck(dialog->isVisible() && dialog->settingsError->isVisible() && dialog->budget->value() == 0.8 &&
                      dialog->separateFolders->isChecked() && !dialog->presetFolders[0]->text().isEmpty() &&
                      ctl.applied_.budgetGb == 0.75 && testPersistedSettings({0.75, utf8(output), false}),
                  "failed persistence retains applied config, pending edits and open window");
        const auto expected = std::find(ctl.errors_.begin(), ctl.errors_.end(),
                                        QString("Could not persist replay settings to the current OBS profile"));
        if (expected != ctl.errors_.end()) ctl.errors_.erase(expected);
        testCheck(QDir().rmdir(blockedTmp), "remove write-failure obstruction");
      }
      dialog->separateFolders->setChecked(false);
      dialog->presetFolders[0]->clear();
      dialog->settingsButtons->button(QDialogButtonBox::Apply)->click();
      testCheck(testPersistedSettings({0.8, utf8(output), false}) && !dialog->settingsError->isVisible(),
                "failed save can be retried");
      dialog->budget->setValue(0.9);
      dialog->separateFolders->setChecked(true);
      dialog->presetFolders[0]->setText(output + "/discard on profile switch");
      obs_frontend_set_current_profile("Replay Settings Test");
    });
    at(1750, [=] {
      testCheck(!dialog->isVisible() && ctl.applied_.budgetGb == 0.33 && ctl.draftSettings() == ctl.applied_,
                "profile switch closes the window, discards edits and loads the other profile");
      testCheck(ctl.applied_.shareEncoder && !dialog->separateEncoder->isChecked(),
                "profile without encoder preference defaults to sharing with checkbox unchecked");
      ctl.settingsAction_->trigger();
      dialog->budget->setValue(0.25);
      dialog->separateEncoder->setChecked(true);
      dialog->settingsButtons->button(QDialogButtonBox::Apply)->click();
      testCheck(!ctl.applied_.shareEncoder && dialog->separateEncoder->isChecked() &&
                    testPersistedSettings(ctl.applied_),
                "checked separate encoder commits independent mode with the existing profile key");
      dialog->budget->setValue(0.5);
      dialog->folder->setText(output + "/unapplied");
      dialog->separateEncoder->setChecked(false);
      dialog->separateFolders->setChecked(true);
      dialog->presetFolders[0]->setText(output + "/pending while active");
      obs_hotkey_trigger_routed_callback(ctl.hotkeys_->id(HotkeyAction::Start), true);
    });
    at(3500, [=] {
      const auto status = ctl.capture_.status();
      testCheck(status.collecting && !status.shared && ctl.sessionFolders_[0] == filePath(output) &&
                    ctl.applied_.budgetGb == 0.25,
                "hotkey starts buffering with applied folder, budget and sharing");
      testCheck(!dialog->budget->isEnabled() && !dialog->folder->isEnabled() && !dialog->browse->isEnabled() &&
                    !dialog->separateEncoder->isEnabled() &&
                    !dialog->settingsButtons->button(QDialogButtonBox::Apply)->isEnabled() &&
                    !dialog->settingsButtons->button(QDialogButtonBox::Ok)->isEnabled() &&
                    !dialog->separateFolders->isEnabled() && !dialog->presetFolders[0]->isEnabled() &&
                    !dialog->presetBrowse[0]->isEnabled() && !ctl.applySettings(),
                "buffering locks fields and prevents commits");
      dialog->tabs->setCurrentIndex(1);
      dock->settingsShortcut->click();
      testCheck(dock->settingsShortcut->isEnabled() && dialog->isVisible() && dialog->tabs->currentIndex() == 1 &&
                    dialog->budget->value() == 0.5 && !(ctl.draftSettings() == ctl.applied_),
                "dock shortcut stays available while buffering and retains pending edits");
      testCheck(dialog->tabs->isEnabled() && dialog->details->text().contains("0.25 GB") && status.available > 0,
                "Diagnostics stays accessible and reports applied settings");
      testNote("settings while buffering");
      testSave(0);
    });
    at(4500, [=] {
      ctl.saveReplay(0, clockNow());
      ctl.stopBuffer();
      dock->settingsShortcut->click();
      testCheck(dock->settingsShortcut->isEnabled() && dialog->isVisible() && dialog->tabs->currentIndex() == 1 &&
                    dialog->budget->value() == 0.5 && !(ctl.draftSettings() == ctl.applied_),
                "dock shortcut stays available while draining and retains pending edits");
      testCheck(ctl.capture_.status().draining &&
                    !dialog->settingsButtons->button(QDialogButtonBox::Apply)->isEnabled() && !ctl.applySettings(),
                "draining prevents commits");
    });
    at(7500, [=] {
      testCheck(!ctl.capture_.status().draining && dialog->budget->value() == 0.5 &&
                    !dialog->separateEncoder->isChecked() &&
                    dialog->settingsButtons->button(QDialogButtonBox::Apply)->isEnabled(),
                "pending edits survive buffering and unlock after draining");
      dialog->settingsButtons->button(QDialogButtonBox::Cancel)->click();
      ctl.startBuffer();
      dock->settingsShortcut->click();
      testCheck(dialog->isVisible() && dialog->tabs->currentIndex() == 0 && ctl.capture_.status().collecting,
                "dock shortcut opens a closed window while buffering");
      testCheck(dialog->settingsButtons->button(QDialogButtonBox::Ok)->isEnabled(),
                "clean OK remains available while buffering");
      dialog->settingsButtons->button(QDialogButtonBox::Ok)->click();
      testCheck(!dialog->isVisible() && ctl.capture_.status().collecting, "closing the window leaves buffering active");
    });
    at(9000, [=] { ctl.stopBuffer(); });
    at(12000, [=] {
      ctl.settingsAction_->trigger();
      dialog->budget->setValue(0.95);
      dialog->separateFolders->setChecked(true);
      dialog->presetFolders[0]->setText(output + "/unapplied shutdown");
      testNote("pending settings before shutdown");
      testFinish(output);
    });
  }
  void testFolderError(int preset, const QString &folder)
  {
    const auto found = std::find_if(ctl.errors_.begin(), ctl.errors_.end(), [=](const QString &message) {
      return message.startsWith(QString("Preset %1 — ").arg(preset + 1)) && message.contains(folder);
    });
    testCheck(found != ctl.errors_.end(), "failed save identifies its preset and destination");
    if (found != ctl.errors_.end()) {
      expectedFolderErrors_.append(*found);
      ctl.errors_.erase(found);
    }
    testCheck(ctl.capture_.status().collecting, "a bad destination leaves buffering active");
  }
  void runFolderTest(const QString &output)
  {
    const auto obsFolder = output + "/../recordings";
    const QString shared = output + "/Shared", a = output + "/Preset 1 α", b = output + "/Preset 2 β",
                  c = output + "/Preset 3 γ";
    const QString missing = output + "/Missing", disconnected = output + "/Disconnect me",
                  readOnly = output + "/Read only";
    for (const auto &path : {shared, a, b, c, disconnected, readOnly})
      testCheck(QDir().mkpath(path), "create scratch replay destination");
    testCheck(QFile::setPermissions(readOnly, QFileDevice::ReadOwner | QFileDevice::ExeOwner),
              "make an unwritable scratch folder");
    ctl.settingsAction_->trigger();
    for (int i = 0; i < 3; ++i) dock->rows[i].seconds->setValue(2);
    dock->rows[0].name->setText("Short α");
    dock->rows[1].name->setText("Medium β");
    dock->rows[2].name->setText("Long γ");
    dialog->separateFolders->setChecked(true);
    dialog->presetFolders[0]->setText(a);
    dialog->separateFolders->setChecked(false);
    testCheck(dialog->presetFolders[0]->text() == a && dialog->folder->isEnabled(),
              "turning separate folders off retains paths");
    dialog->separateFolders->setChecked(true);
    testCheck(dialog->presetFolders[0]->text() == a && !dialog->folder->isEnabled() && !dialog->browse->isEnabled() &&
                  dialog->presetFolders[0]->isEnabled(),
              "turning separate folders on restores paths and disables shared controls");
    dialog->settingsButtons->button(QDialogButtonBox::Cancel)->click();
    testCheck(!dialog->separateFolders->isChecked() && dialog->presetFolders[0]->text().isEmpty(),
              "Cancel discards folder mode and path edits");
    ctl.settingsAction_->trigger();
    for (int phase = 0; phase < 9; ++phase) {
      const int base = 250 + phase * 6000;
      at(base, [=] {
        dialog->separateFolders->setChecked(phase >= 2);
        dialog->folder->setText(phase == 0 ? "" : shared);
        dialog->presetFolders[0]->setText(phase >= 3 ? a : "");
        dialog->presetFolders[1]->setText(phase >= 4 ? b : "");
        dialog->presetFolders[2]->setText(phase >= 5 ? c : "");
        if (phase == 6) dialog->presetFolders[0]->setText(missing);
        if (phase == 7) dialog->presetFolders[2]->setText(disconnected);
        if (phase == 8) dialog->presetFolders[2]->setText(readOnly);
        testCheck(ctl.applySettings() && testPersistedSettings(ctl.applied_),
                  "Apply persists folder mode and all paths");
        if (phase == 5) dialog->grab().save(output + "/preset-folders-settings.png");
        ctl.startBuffer();
        testCheck(ctl.capture_.status().collecting, "buffer starts for the selected folder configuration");
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
          routed &= ctl.sessionFolders_[i].lexically_normal() == filePath(expected[i]).lexically_normal();
        testCheck(routed, "each session resolves independent OBS and custom destinations");
        testCheck(!dialog->separateFolders->isEnabled() && !dialog->presetFolders[0]->isEnabled() &&
                      !dialog->presetBrowse[2]->isEnabled(),
                  "folder controls lock during buffering");
        if (phase == 5) {
          dialog->tabs->setCurrentIndex(1);
          testCheck(dialog->encoder->text().contains(a) && dialog->encoder->text().contains(b) &&
                        dialog->encoder->text().contains(c),
                    "Diagnostics lists all three active destinations");
        }
        testNote(qPrintable(QString("folder phase %1").arg(phase)));
      });
      at(base + 1700, [=] {
        if (phase == 7) testCheck(QDir().rmdir(disconnected), "disconnect one folder while buffering");
        if (phase == 5) {
          dialog->presetFolders[0]->setText(output + "/Unapplied");
          testCheck(!ctl.applySettings() && ctl.sessionFolders_[0] == filePath(a),
                    "draft paths cannot redirect an active session");
          dialog->grab().save(output + "/preset-folders-diagnostics.png");
        }
        dock->rows[0].save->click();
        testSave(1);
        dock->rows[2].save->click();
      });
      at(base + 5000, [=] {
        if (phase == 6 || phase == 7) {
          ctl.refresh();
          testFolderError(phase == 6 ? 0 : 2, phase == 6 ? missing : disconnected);
          testCheck(ctl.error_.contains(phase == 6 ? missing : disconnected),
                    "other presets succeeding do not clear a destination error");
        }
        if (phase == 8) {
          ctl.refresh();
          testFolderError(2, readOnly);
        }
        ctl.stopBuffer();
      });
    }
    at(56000, [=] {
      testCheck(!ctl.capture_.status().draining && ctl.capture_.status().memory == 0,
                "multi-folder saves drain and release replay memory");
      testCheck(
          QFile::setPermissions(readOnly, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner),
          "restore scratch folder permissions");
      dialog->separateFolders->setChecked(false);
      dialog->folder->setText(missing);
      testCheck(ctl.applySettings(), "apply missing shared folder while stopped");
      ctl.startBuffer();
      testCheck(!ctl.capture_.status().collecting && ctl.error_.contains("existing directory"),
                "shared mode retains startup validation");
      const auto expected = std::find(ctl.errors_.begin(), ctl.errors_.end(), ctl.error_);
      if (expected != ctl.errors_.end()) ctl.errors_.erase(expected);
      ctl.error_.clear();
      dialog->folder->setText(shared);
      dialog->separateFolders->setChecked(true);
      dialog->presetFolders[0]->setText(a);
      dialog->presetFolders[1]->setText(b);
      dialog->presetFolders[2]->setText(c);
      dialog->settingsButtons->button(QDialogButtonBox::Ok)->click();
      testCheck(!dialog->isVisible() && testPersistedSettings(ctl.applied_), "OK persists preset folders for restart");
      testFinish(output);
    });
  }
  void testSave(int preset) { obs_hotkey_trigger_routed_callback(ctl.hotkeys_->id(saveAction(preset)), true); }
  // Hotkey helpers for the presets scenario: one key per preset hotkey.
  void bindKey(int preset, const char *key)
  {
    auto *binding = obs_data_create();
    obs_data_set_string(binding, "key", key);
    auto *bindings = obs_data_array_create();
    obs_data_array_push_back(bindings, binding);
    obs_hotkey_load(ctl.hotkeys_->id(saveAction(preset)), bindings);
    obs_data_array_release(bindings);
    obs_data_release(binding);
  }
  QString boundKey(int preset)
  {
    auto *bindings = obs_hotkey_save(ctl.hotkeys_->id(saveAction(preset)));
    QString key;
    if (obs_data_array_count(bindings) == 1) {
      auto *binding = obs_data_array_item(bindings, 0);
      key = obs_data_get_string(binding, "key");
      obs_data_release(binding);
    } else if (obs_data_array_count(bindings) > 1) {
      key = "several";
    }
    obs_data_array_release(bindings);
    return key;
  }
  static QStringList registeredPresetHotkeys()
  {
    QStringList names;
    obs_enum_hotkeys(
        [](void *data, obs_hotkey_id, obs_hotkey_t *hotkey) {
          const QString name = obs_hotkey_get_name(hotkey);
          if (name.startsWith("MultiReplay.Preset")) static_cast<QStringList *>(data)->append(name);
          return true;
        },
        &names);
    names.sort();
    return names;
  }
  static QStringList presetHotkeyNames(int count)
  {
    QStringList names;
    for (int i = 0; i < count; ++i) names.append(ids::hotkeys[int(saveAction(i))]);
    return names;
  }
  // Variable preset count. A fresh profile starts with two presets; the run adds up to
  // eight, binds hotkeys, removes preset 3 and saves from the last preset into its own
  // folder. "restore" checks the result after a restart.
  void runPresetTest(const QString &output, bool restore)
  {
    const QString base = output + "/../preset-folders";
    const auto folderFor = [base](int original) { return QDir::cleanPath(base + QString("/Preset %1").arg(original)); };
    const auto nameFor = [](int original) { return QString("Preset %1 δ").arg(original); };
    // Originals 1, 2 and 4–8 remain after preset 3 is removed.
    const std::vector<int> kept = {1, 2, 4, 5, 6, 7, 8};
    const auto keptPresetsMatch = [=] {
      bool match = dock->presetCount() == int(kept.size()) && ctl.applied_.presetFolders.size() == kept.size() &&
                   dialog->presetFolders.size() == kept.size();
      for (size_t i = 0; match && i < kept.size(); ++i)
        match = dock->rows[i].name->text() == nameFor(kept[i]) && dock->rows[i].seconds->value() == kept[i] + 1 &&
                q(ctl.applied_.presetFolders[i]) == folderFor(kept[i]) &&
                dialog->presetFolders[i]->text() == folderFor(kept[i]);
      return match;
    };
    // Original preset 4 is bound to F17 and original 8 to F16; original 3's F15 is gone.
    const auto keptBindingsMatch = [=] {
      bool match = true;
      for (int i = 0; i < int(kept.size()); ++i)
        match &= boundKey(i) == (i == 2 ? "OBS_KEY_F17" : i == 6 ? "OBS_KEY_F16" : "");
      return match;
    };
    if (restore) {
      at(500, [=] {
        testCheck(keptPresetsMatch(), "preset count, names, lengths and folders survive restart");
        testCheck(keptBindingsMatch(), "moved hotkey bindings survive restart");
        testCheck(registeredPresetHotkeys() == presetHotkeyNames(7), "restart registers one save hotkey per preset");
        testCheck(dock->add->isEnabled() && dock->rows[0].remove->isEnabled(), "restored presets can be edited");
        testNote("restored presets");
        testFinish(output);
      });
      return;
    }
    at(500, [=] {
      testCheck(dock->presetCount() == 2 && dock->rows[0].name->text() == "Replay 1" &&
                    dock->rows[0].seconds->value() == 60 && dock->rows[1].name->text() == "Replay 2" &&
                    dock->rows[1].seconds->value() == 240,
                "a new profile starts with two presets");
      testCheck(registeredPresetHotkeys() == presetHotkeyNames(2), "save hotkeys exist only for existing presets");
      testCheck(dock->add->isEnabled() && dock->rows[0].remove->isEnabled() && dock->rows[1].remove->isEnabled(),
                "presets can be added and removed while stopped");
      testNote("fresh presets");
      for (int i = 2; i < maxPresets; ++i) dock->add->click();
      testCheck(dock->presetCount() == maxPresets && !dock->add->isEnabled(), "Add stops at eight presets");
      testCheck(dock->rows[7].name->text() == "Replay 8" && dock->rows[7].seconds->value() == 60,
                "added presets start at 60 seconds");
      testCheck(registeredPresetHotkeys() == presetHotkeyNames(maxPresets), "each added preset gets a save hotkey");
      testCheck(dialog->presetFolders.size() == size_t(maxPresets), "the settings window has a folder per preset");
      for (int i = 0; i < maxPresets; ++i) {
        dock->rows[i].seconds->setValue(i + 2);
        dock->rows[i].name->setText(nameFor(i + 1));
        QMetaObject::invokeMethod(dock->rows[i].name, "editingFinished", Qt::DirectConnection);
        testCheck(QDir().mkpath(folderFor(i + 1)), "create scratch preset folder");
      }
      ctl.settingsAction_->trigger();
      dialog->separateFolders->setChecked(true);
      dialog->budget->setValue(0.25);
      for (int i = 0; i < maxPresets; ++i) dialog->presetFolders[i]->setText(folderFor(i + 1));
      testCheck(ctl.applySettings() && testPersistedSettings(ctl.applied_), "Apply persists eight preset folders");
      bindKey(2, "OBS_KEY_F15");
      bindKey(3, "OBS_KEY_F17");
      bindKey(7, "OBS_KEY_F16");
    });
    at(1000, [=] {
      testNote("eight presets");
      dock->grab().save(output + "/presets-eight.png");
      dialog->grab().save(output + "/presets-settings.png");
      dialog->budget->setValue(0.5); // An unapplied edit that removing a preset must keep.
      dock->rows[2].remove->click();
      testCheck(keptPresetsMatch(), "removing a preset moves later names, lengths and folders up");
      testCheck(keptBindingsMatch(), "hotkey bindings move with their presets");
      testCheck(registeredPresetHotkeys() == presetHotkeyNames(7), "removing a preset removes the last save hotkey");
      testCheck(dialog->budget->value() == 0.5 && ctl.draftSettings() != ctl.applied_,
                "removing a preset keeps unapplied window edits");
      dialog->settingsButtons->button(QDialogButtonBox::Cancel)->click();
      auto *data = prefs::loadFromProfile(obs_frontend_get_profile_config());
      auto *moved = obs_data_get_array(data, prefs::hotkeyKey(int(saveAction(6))).c_str());
      testCheck(prefs::presetCount(data) == 7 && QString(obs_data_get_string(data, "name2")) == nameFor(4) &&
                    !obs_data_has_user_value(data, "name7") && !obs_data_has_user_value(data, "hotkey9") && moved &&
                    obs_data_array_count(moved) == 1,
                "the profile stores seven presets and the moved binding");
      obs_data_array_release(moved);
      obs_data_release(data);
      testNote("preset 3 removed");
    });
    at(1500, [=] {
      ctl.startBuffer();
      testCheck(ctl.capture_.status().collecting, "buffer starts with seven presets");
    });
    at(2000, [=] {
      bool locked = !dock->add->isEnabled();
      for (const auto &row : dock->rows)
        locked &= !row.remove->isEnabled() && !row.seconds->isEnabled() && !row.name->isEnabled();
      testCheck(locked, "presets cannot be added, removed or edited while buffering");
      ctl.addPreset();
      ctl.removePreset(0);
      testCheck(dock->presetCount() == 7, "add and remove are refused while buffering");
      testCheck(ctl.sessionFolders_.size() == 7 &&
                    ctl.sessionFolders_[6].lexically_normal() == filePath(folderFor(8)).lexically_normal(),
                "each preset has its own session folder");
      testNote("presets locked");
    });
    at(11500, [=] {
      testSave(6);
      dock->rows[0].save->click();
    });
    at(12500, [=] { ctl.stopBuffer(); });
    at(16500, [=] {
      ctl.refresh();
      testCheck(!ctl.capture_.status().draining && dock->add->isEnabled() && dock->rows[0].remove->isEnabled(),
                "presets unlock after draining");
      testNote("presets stopped");
      testFinish(output);
    });
  }
  void testFinish(const QString &output)
  {
    auto status = ctl.capture_.status();
    QJsonObject report;
    report["memory_after_stop"] = double(status.memory);
    report["results"] = int(ctl.capture_.recentResults().size());
    report["collecting"] = status.collecting;
    report["draining"] = status.draining;
    report["encoder"] = dialog->encoder->text();
    report["steps"] = steps_;
    report["checks"] = checks_;
    report["expected_folder_errors"] = expectedFolderErrors_;
    QJsonArray saved, errors;
    for (auto &result : ctl.testResults_) {
      QJsonObject value;
      value["path"] = pathText(result.path);
      value["error"] = q(result.error);
      value["partial"] = q(result.partial);
      value["requested"] = double(result.requested) / 1e6;
      value["actual"] = double(result.actual) / 1e6;
      value["end_delta_ms"] = double(result.mediaEnd - result.endpoint) / 1e3;
      saved.append(value);
    }
    for (auto &message : ctl.errors_) errors.append(message);
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
    if (mode == "reset") {
      at(500, [=] {
        ctl.startBuffer();
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
      at(17500, [=] { ctl.stopBuffer(); });
      at(20000, [=] {
        ctl.startBuffer();
        testNote("replay restarted");
      });
      at(25000, [=] {
        testNote("sharing the new encoder");
        testSave(0);
      });
      at(26500, [=] { ctl.stopBuffer(); });
      at(29000, [=] { testFinish(output); });
      return;
    }
    if (mode == "long") {
      at(500, [=] {
        ctl.startBuffer();
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
      at(668000, [this, output] { dock->grab().save(output + "/dock.png"); });
      at(670000, [=] { obs_frontend_recording_stop(); });
      at(673000, [=] {
        testNote("recording stopped");
        ctl.stopBuffer();
      });
      at(680000, [=] { testFinish(output); });
      return;
    }
    at(500, [=] {
      ctl.startBuffer();
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
    at(26000, [this, output] { dock->grab().save(output + "/dock.png"); });
    at(26500, [=] { ctl.stopBuffer(); });
    at(29000, [=] {
      testNote("replay stopped");
      obs_frontend_recording_start();
    });
    at(32000, [=] {
      ctl.startBuffer();
      testNote("replay joined a running recording");
    });
    at(38000, [=] { testSave(0); });
    at(39500, [=] { ctl.stopBuffer(); });
    at(41500, [=] {
      testNote("replay stopped, recording must continue");
      obs_frontend_recording_stop();
    });
    at(44500, [=] { testFinish(output); });
  }

private:
  void at(int ms, std::function<void()> step) { QTimer::singleShot(ms, this, std::move(step)); }
  ReplayController &ctl;
  ReplayDock *dock;
  SettingsDialog *dialog;
  QJsonArray steps_, checks_, expectedFolderErrors_;
};

void ReplayController::runIntegrationTest() { (new IntegrationDriver(*this))->run(); }
} // namespace replay
