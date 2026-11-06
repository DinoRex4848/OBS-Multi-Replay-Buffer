// OBS module entry points: registers the replay output, then creates the dock,
// Tools menu entry and hotkeys once the frontend has loaded.
#include "capture.hpp"
#include "identifiers.hpp"
#include "replay_controller.hpp"
#include <obs-frontend-api.h>
#include <util/bmem.h>
#include <QPointer>
#include <QWidget>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-multi-replay", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
  return "Save multiple replay lengths from one encoded RAM history";
}
MODULE_EXPORT const char *obs_module_name(void) { return "Multi-Length Replay"; }
MODULE_EXPORT const char *obs_module_author(void) { return "Multi-Length Replay contributors"; }

namespace {
using replay::ReplayController;
QPointer<ReplayController> controller;
void frontendEvent(obs_frontend_event event, void *)
{
  if (!controller) return;
  switch (event) {
  case OBS_FRONTEND_EVENT_PROFILE_CHANGING: controller->profileChanging(); break;
  case OBS_FRONTEND_EVENT_PROFILE_CHANGED: controller->profileChanged(); break;
  case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING: controller->stopBuffer(); break;
  case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED: controller->builtInStarted(); break;
  case OBS_FRONTEND_EVENT_RECORDING_PAUSED: controller->recordingPaused(true); break;
  case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
  case OBS_FRONTEND_EVENT_RECORDING_STOPPED: controller->recordingPaused(false); break;
  case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN: controller->shutdown(); break;
  case OBS_FRONTEND_EVENT_EXIT: controller->shutdown(); break;
#ifdef REPLAY_INTEGRATION_TEST
  case OBS_FRONTEND_EVENT_FINISHED_LOADING: controller->runIntegrationTest(); break;
#endif
  default: break;
  }
}
void saveCallback(obs_data_t *, bool saving, void *)
{
  if (controller && saving) controller->saveSettings();
}
} // namespace

bool obs_module_load(void)
{
  if (obs_get_version() < MAKE_SEMANTIC_VERSION(32, 1, 0)) {
    blog(LOG_ERROR, "[multi-replay] OBS 32.1 or newer is required");
    return false;
  }
  replay::registerReplayOutput();
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
  if (!obs_frontend_add_dock_by_id(replay::ids::dock, "Multi-Length Replay", content->controls())) {
    blog(LOG_ERROR, "[multi-replay] Cannot register replay dock");
    delete content;
    return;
  }
  if (!content->addSettingsMenu()) {
    blog(LOG_ERROR, "[multi-replay] Cannot register Tools settings action");
    // Shut down while the dock still exists: removing it deletes the dock's widgets.
    content->shutdown();
    obs_frontend_remove_dock(replay::ids::dock);
    delete content;
    return;
  }
  controller = content;
  obs_frontend_add_event_callback(frontendEvent, nullptr);
  obs_frontend_add_save_callback(saveCallback, nullptr);
  blog(LOG_INFO, "[multi-replay] Replay dock and Tools settings ready; manual startup, new profiles start with 60/240 "
                 "second presets");
}
void obs_module_unload(void)
{
  obs_frontend_remove_event_callback(frontendEvent, nullptr);
  obs_frontend_remove_save_callback(saveCallback, nullptr);
  if (controller) {
    controller->shutdown();
    obs_frontend_remove_dock(replay::ids::dock);
    delete controller.data();
    controller.clear();
  }
}
