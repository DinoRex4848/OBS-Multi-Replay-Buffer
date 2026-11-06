#pragma once
#include "capture.hpp"
#include <obs-frontend-api.h>
#include <util/config-file.h>

namespace replay {
struct Encoders {
  obs_encoder_t *video = nullptr;
  std::vector<obs_encoder_t *> audio;
  std::string description, fingerprint;
  // shared: the encoders are the OBS frontend's own recording encoders.
  // shareNote: why sharing was requested but an independent encoder is used.
  bool shared = false;
  std::string shareNote;
  std::vector<std::string> trackNames;
  std::filesystem::path folder;
  FilenameOptions filenames;
  obs_video_info videoInfo{};
  Encoders() = default;
  ~Encoders();
  Encoders(Encoders &&) noexcept;
  Encoders &operator=(Encoders &&) noexcept;
  Encoders(const Encoders &) = delete;
};
bool encoderAvailable(const std::string &id);
Encoders recordingEncoders(bool share = false);
// New reference to the video encoder the frontend would record with now, or null.
obs_encoder_t *frontendRecordingVideo();
std::string recordingFingerprint();
}
