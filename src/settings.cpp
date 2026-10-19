#include "settings.hpp"
#include <util/bmem.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace replay {
namespace {
struct Data {
  obs_data_t *p;
  explicit Data(obs_data_t *value = nullptr) : p(value ? value : obs_data_create()) {}
  ~Data() { obs_data_release(p); }
};
std::string get(config_t *config, const char *section, const char *key)
{
  auto *value = config_get_string(config, section, key);
  return value ? value : "";
}
std::filesystem::path profilePath()
{
  char *path = obs_frontend_get_current_profile_path();
  if (!path) throw std::runtime_error("No OBS recording profile is available");
  auto result = std::filesystem::u8path(path);
  bfree(path);
  return result;
}
obs_data_t *encoderFile(const char *name)
{
  auto path = profilePath() / name;
  if (!std::filesystem::exists(path)) return obs_data_create();
  auto *data = obs_data_create_from_json_file(path.u8string().c_str());
  if (!data) throw std::runtime_error(std::string("Cannot read OBS profile's ") + name);
  return data;
}
obs_encoder_t *createVideo(const std::string &id, obs_data_t *settings)
{
  if (!encoderAvailable(id)) throw std::runtime_error("Recording video encoder is unavailable: " + id);
  auto *encoder = obs_video_encoder_create(id.c_str(), "Multi-Replay video", settings, nullptr);
  if (!encoder) throw std::runtime_error("Cannot create recording video encoder: " + id);
  std::string codec = obs_encoder_get_codec(encoder);
  if (codec != "h264" && codec != "hevc" && codec != "av1") {
    obs_encoder_release(encoder);
    throw std::runtime_error("Unsupported recording video codec: " + codec);
  }
  obs_encoder_set_video(encoder, obs_get_video());
  return encoder;
}
int closestBitrate(const std::string &id, int requested)
{
  auto *props = obs_get_encoder_properties(id.c_str());
  if (!props) return requested;
  auto *prop = obs_properties_get(props, "bitrate");
  int chosen = requested;
  if (prop && obs_property_get_type(prop) == OBS_PROPERTY_LIST) {
    // Same rule as the frontend: exact match, else next higher, else next lower.
    int lower = 0, higher = 0;
    for (size_t i = 0; i < obs_property_list_item_count(prop); ++i) {
      if (obs_property_list_item_disabled(prop, i)) continue;
      int value = int(obs_property_list_item_int(prop, i));
      if (value >= requested && (!higher || value < higher)) higher = value;
      if (value < requested && value > lower) lower = value;
    }
    chosen = higher ? higher : lower ? lower : 192;
  }
  obs_properties_destroy(props);
  return chosen;
}
void requireAudioCodec(obs_encoder_t *encoder)
{
  std::string codec = obs_encoder_get_codec(encoder);
  if (codec != "aac" && codec != "flac" && codec != "opus" && codec != "pcm_s16le" && codec != "pcm_s24le" &&
      codec != "pcm_f32le")
    throw std::runtime_error("Unsupported recording audio codec: " + codec);
}
obs_encoder_t *createAudio(const std::string &id, const std::string &name, int mix, int bitrate)
{
  if (!encoderAvailable(id)) throw std::runtime_error("Recording audio encoder is unavailable: " + id);
  Data data;
  obs_data_set_int(data.p, "bitrate", closestBitrate(id, bitrate));
  obs_data_set_string(data.p, "rate_control", "CBR");
  auto *encoder = obs_audio_encoder_create(id.c_str(), name.c_str(), data.p, size_t(mix), nullptr);
  if (!encoder) throw std::runtime_error("Cannot create recording audio encoder: " + id);
  try {
    requireAudioCodec(encoder);
  } catch (...) {
    obs_encoder_release(encoder);
    throw;
  }
  obs_encoder_set_audio(encoder, obs_get_audio());
  return encoder;
}
// Raised when the frontend's own recording encoders cannot be used; the caller
// then falls back to independent encoders and reports the reason.
struct ShareUnavailable : std::runtime_error {
  using std::runtime_error::runtime_error;
};
// The frontend creates its recording encoders whenever it resets its outputs
// and attaches them to the recording output the first time one of its outputs
// starts. Until then they are reachable by their fixed creation names.
struct Frontend {
  obs_output_t *recording = obs_frontend_get_recording_output();
  bool attached = recording && obs_output_get_video_encoder(recording);
  Frontend() = default;
  Frontend(const Frontend &) = delete;
  ~Frontend()
  {
    if (recording) obs_output_release(recording);
  }
  obs_encoder_t *video(const char *name) const
  {
    return attached ? obs_encoder_get_ref(obs_output_get_video_encoder(recording)) : obs_get_encoder_by_name(name);
  }
  obs_encoder_t *audio(size_t index, const std::string &name) const
  {
    if (!attached) return obs_get_encoder_by_name(name.c_str());
    auto *encoder = obs_output_get_audio_encoder(recording, index);
    return encoder ? obs_encoder_get_ref(encoder) : nullptr;
  }
};
// Applies what the frontend itself applies when a recording starts. An encoder
// that is already running keeps its configuration, exactly as it does for OBS.
obs_encoder_t *shareVideo(const Frontend &frontend, const char *name, const std::string &id, obs_data_t *settings)
{
  auto *encoder = frontend.video(name);
  if (!encoder) throw ShareUnavailable("OBS's recording video encoder was not found");
  if (obs_encoder_get_type(encoder) != OBS_ENCODER_VIDEO || id != obs_encoder_get_id(encoder)) {
    obs_encoder_release(encoder);
    throw ShareUnavailable("OBS's recording encoder does not match the saved recording settings");
  }
  std::string codec = obs_encoder_get_codec(encoder);
  if (codec != "h264" && codec != "hevc" && codec != "av1") {
    obs_encoder_release(encoder);
    throw std::runtime_error("Unsupported recording video codec: " + codec);
  }
  if (!obs_encoder_active(encoder)) {
    obs_encoder_update(encoder, settings);
    obs_encoder_set_video(encoder, obs_get_video());
  }
  return encoder;
}
obs_encoder_t *shareAudio(const Frontend &frontend, size_t index, const std::string &name, const std::string &id,
                          int mix, obs_data_t *settings)
{
  auto *encoder = frontend.audio(index, name);
  if (!encoder)
    throw ShareUnavailable("OBS's recording audio encoder for track " + std::to_string(mix + 1) + " was not found");
  if (obs_encoder_get_type(encoder) != OBS_ENCODER_AUDIO || id != obs_encoder_get_id(encoder) ||
      obs_encoder_get_mixer_index(encoder) != size_t(mix)) {
    obs_encoder_release(encoder);
    throw ShareUnavailable("OBS's recording audio tracks do not match the saved recording settings");
  }
  try {
    requireAudioCodec(encoder);
  } catch (...) {
    obs_encoder_release(encoder);
    throw;
  }
  if (!obs_encoder_active(encoder)) {
    obs_encoder_update(encoder, settings);
    obs_encoder_set_audio(encoder, obs_get_audio());
  }
  return encoder;
}
std::string simpleVideoId(const std::string &id)
{
  if (id == "x264" || id == "x264_lowcpu") return "obs_x264";
  if (id == "nvenc") return encoderAvailable("obs_nvenc_h264_tex") ? "obs_nvenc_h264_tex" : "ffmpeg_nvenc";
  if (id == "nvenc_hevc") return encoderAvailable("obs_nvenc_hevc_tex") ? "obs_nvenc_hevc_tex" : "ffmpeg_hevc_nvenc";
  if (id == "nvenc_av1") return "obs_nvenc_av1_tex";
  throw std::runtime_error(
      "Simple Output mapping is implemented for NVIDIA and x264; use Advanced Standard Output for " + id);
}
std::string simpleAudioId(const std::string &codec)
{
  if (codec == "opus") return "ffmpeg_opus";
  // OBS 32.1.1 selects AAC encoders by available bitrate, in this preference order.
  for (auto *id : {"CoreAudio_AAC", "libfdk_aac", "ffmpeg_aac"})
    if (encoderAvailable(id)) return id;
  throw std::runtime_error("No OBS AAC encoder is available");
}
void rescale(obs_encoder_t *encoder, config_t *config, const char *key, const char *filterKey)
{
  if (obs_encoder_active(encoder)) return;
  int filter = int(config_get_int(config, "AdvOut", filterKey));
  unsigned width = 0, height = 0;
  auto resolution = get(config, "AdvOut", key);
  // Like the frontend, an unreadable resolution means no rescaling.
  if (filter == OBS_SCALE_DISABLE || sscanf(resolution.c_str(), "%ux%u", &width, &height) != 2) width = height = 0;
  obs_encoder_set_scaled_size(encoder, width, height);
  obs_encoder_set_gpu_scale_type(encoder, obs_scale_type(filter));
}
}
bool encoderAvailable(const std::string &id)
{
  const char *candidate;
  for (size_t i = 0; obs_enum_encoder_types(i, &candidate); ++i)
    if (id == candidate) return true;
  return false;
}
Encoders::~Encoders()
{
  if (video) obs_encoder_release(video);
  for (auto *encoder : audio) obs_encoder_release(encoder);
}
Encoders::Encoders(Encoders &&other) noexcept { *this = std::move(other); }
Encoders &Encoders::operator=(Encoders &&other) noexcept
{
  if (this != &other) {
    if (video) obs_encoder_release(video);
    for (auto *encoder : audio) obs_encoder_release(encoder);
    video = std::exchange(other.video, nullptr);
    audio = std::move(other.audio);
    other.audio.clear();
    shared = other.shared;
    shareNote = std::move(other.shareNote);
    trackNames = std::move(other.trackNames);
    folder = std::move(other.folder);
    filenames = std::move(other.filenames);
    description = std::move(other.description);
    fingerprint = std::move(other.fingerprint);
    videoInfo = other.videoInfo;
  }
  return *this;
}
std::string recordingFingerprint()
{
  auto *config = obs_frontend_get_profile_config();
  if (!config) return {};
  std::ostringstream text;
  const std::vector<std::pair<const char *, std::vector<const char *>>> keys = {
      {"Output", {"Mode"}},
      {"AdvOut",
       {"RecType", "RecEncoder", "RecAudioEncoder", "RecTracks", "RecRescaleFilter", "RecRescaleRes", "RecFormat2",
        "FLVTrack", "Encoder", "AudioEncoder", "RescaleFilter", "RescaleRes", "TrackIndex", "VodTrackIndex",
        "ApplyServiceSettings"}},
      {"SimpleOutput",
       {"RecQuality", "RecEncoder", "RecAudioEncoder", "RecTracks", "RecFormat2", "StreamEncoder", "StreamAudioEncoder",
        "VBitrate", "ABitrate", "UseAdvanced", "Preset", "NVENCPreset2", "x264Settings"}},
      {"Video",
       {"OutputCX", "OutputCY", "FPSType", "FPSCommon", "FPSNum", "FPSDen", "ColorFormat", "ColorSpace", "ColorRange",
        "HdrNominalPeakLevel"}},
      {"Audio", {"SampleRate", "ChannelSetup"}},
      {"Stream1", {"IgnoreRecommended"}}};
  for (auto &section : keys)
    for (auto *name : section.second)
      text << section.first << ':' << name << '=' << get(config, section.first, name) << '\n';
  for (int i = 1; i <= 6; ++i)
    for (auto *suffix : {"Name", "Bitrate"})
      text << get(config, "AdvOut", ("Track" + std::to_string(i) + suffix).c_str()) << '\n';
  for (auto *name : {"recordEncoder.json", "streamEncoder.json"}) {
    Data data(encoderFile(name));
    text << obs_data_get_json(data.p);
  }
  return text.str();
}
namespace {
Encoders build(bool share)
{
  Encoders result;
  Frontend frontend;
  auto *config = obs_frontend_get_profile_config();
  if (!config || !obs_get_video() || !obs_get_audio())
    throw std::runtime_error("OBS audio/video is not initialized yet");
  obs_get_video_info(&result.videoInfo);
  const bool advanced = get(config, "Output", "Mode") == "Advanced";
  result.filenames.format = get(config, "Output", "FilenameFormatting");
  result.filenames.prefix = get(config, "SimpleOutput", "RecRBPrefix");
  result.filenames.suffix = get(config, "SimpleOutput", "RecRBSuffix");
  result.filenames.allowSpaces = !config_get_bool(config, advanced ? "AdvOut" : "SimpleOutput",
                                                  advanced ? "RecFileNameWithoutSpace" : "FileNameWithoutSpace");
  int tracks = 0, bitrate = 192;
  std::string audioId;
  if (advanced) {
    if (get(config, "AdvOut", "RecType") != "Standard")
      throw std::runtime_error("Custom FFmpeg Output is unsupported; choose Advanced → Standard recording");
    auto videoId = get(config, "AdvOut", "RecEncoder");
    bool streamVideo = videoId == "none";
    if (streamVideo) videoId = get(config, "AdvOut", "Encoder");
    Data settings(encoderFile(streamVideo ? "streamEncoder.json" : "recordEncoder.json"));
    if (streamVideo) {
      // The supported public streaming output may already have resolved service
      // settings. Clone its SETTINGS, never its active encoder instance.
      auto *output = obs_frontend_get_streaming_output();
      auto *encoder = output ? obs_output_get_video_encoder(output) : nullptr;
      if (encoder && obs_output_active(output)) {
        Data effective(obs_encoder_get_settings(encoder));
        obs_data_apply(settings.p, effective.p);
      } else {
        Data audioSettings;
        auto *service = obs_frontend_get_streaming_service();
        if (service && config_get_bool(config, "AdvOut", "ApplyServiceSettings")) {
          int original = int(obs_data_get_int(settings.p, "bitrate"));
          obs_service_apply_encoder_settings(service, settings.p, audioSettings.p);
          if (config_get_bool(config, "Stream1", "IgnoreRecommended"))
            obs_data_set_int(settings.p, "bitrate", original);
        }
      }
      if (output) obs_output_release(output);
    }
    audioId = get(config, "AdvOut", "RecAudioEncoder");
    if (share && (streamVideo || audioId == "none"))
      throw ShareUnavailable("OBS is set to record with the streaming encoder");
    result.video = share ? shareVideo(frontend, "advanced_video_recording", videoId, settings.p)
                         : createVideo(videoId, settings.p);
    rescale(result.video, config, streamVideo ? "RescaleRes" : "RecRescaleRes",
            streamVideo ? "RescaleFilter" : "RecRescaleFilter");
    if (audioId == "none") audioId = get(config, "AdvOut", "AudioEncoder");
    const bool flv = get(config, "AdvOut", "RecFormat2") == "flv";
    tracks = int(config_get_int(config, "AdvOut", flv ? "FLVTrack" : "RecTracks"));
    // The frontend's fallback for legacy profiles with no recording track selected.
    if (!tracks) tracks = int(config_get_int(config, "AdvOut", "TrackIndex"));
    if (flv) tracks = tracks >= 1 && tracks <= 6 ? 1 << (tracks - 1) : 0;
    result.folder = std::filesystem::u8path(get(config, "AdvOut", "RecFilePath"));
  } else {
    auto quality = get(config, "SimpleOutput", "RecQuality");
    if (quality == "Lossless")
      throw std::runtime_error("Simple Lossless uses custom FFmpeg Output; choose a standard encoded recording mode");
    const bool stream = quality == "Stream";
    if (share && stream) throw ShareUnavailable("OBS is set to record with the streaming encoder");
    auto simpleId = get(config, "SimpleOutput", stream ? "StreamEncoder" : "RecEncoder");
    auto videoId = simpleVideoId(simpleId);
    Data settings;
    if (stream) {
      bitrate = int(config_get_int(config, "SimpleOutput", "ABitrate"));
      obs_data_set_string(settings.p, "rate_control", "CBR");
      obs_data_set_int(settings.p, "bitrate", config_get_int(config, "SimpleOutput", "VBitrate"));
      auto preset = get(config, "SimpleOutput", simpleId.find("nvenc") == 0 ? "NVENCPreset2" : "Preset");
      obs_data_set_string(settings.p, videoId.find("ffmpeg_") == 0 ? "preset2" : "preset", preset.c_str());
      if (config_get_bool(config, "SimpleOutput", "UseAdvanced"))
        obs_data_set_string(settings.p, "x264opts", get(config, "SimpleOutput", "x264Settings").c_str());
      Data audioSettings;
      obs_data_set_int(audioSettings.p, "bitrate", bitrate);
      if (auto *service = obs_frontend_get_streaming_service()) {
        int original = int(obs_data_get_int(settings.p, "bitrate"));
        obs_service_apply_encoder_settings(service, settings.p, audioSettings.p);
        if (config_get_bool(config, "Stream1", "IgnoreRecommended"))
          obs_data_set_int(settings.p, "bitrate", original);
        else
          bitrate = int(obs_data_get_int(audioSettings.p, "bitrate"));
      }
      audioId = simpleAudioId(get(config, "SimpleOutput", "StreamAudioEncoder"));
      tracks = 1;
    } else {
      if (quality != "HQ" && quality != "Small")
        throw std::runtime_error("Unknown Simple recording quality: " + quality);
      int crf = quality == "HQ" ? 16 : 23;
      if (simpleId == "x264_lowcpu") crf -= 2;
      auto &vi = result.videoInfo;
      crf -=
          int((1.0 - std::min(2000.0, std::hypot(double(vi.output_width), double(vi.output_height))) / 2000.0) * 10.0);
      if (videoId == "obs_x264") {
        obs_data_set_string(settings.p, "rate_control", "CRF");
        obs_data_set_int(settings.p, "crf", crf);
        obs_data_set_string(settings.p, "profile", "high");
        obs_data_set_bool(settings.p, "use_bufsize", true);
        obs_data_set_string(settings.p, "preset", simpleId == "x264_lowcpu" ? "ultrafast" : "veryfast");
      } else {
        obs_data_set_string(settings.p, "rate_control", "CQP");
        obs_data_set_int(settings.p, "cqp", crf);
        obs_data_set_string(settings.p, "profile", simpleId == "nvenc" ? "high" : "main");
      }
      audioId = simpleAudioId(get(config, "SimpleOutput", "RecAudioEncoder"));
      tracks = int(config_get_int(config, "SimpleOutput", "RecTracks"));
      if (get(config, "SimpleOutput", "RecFormat2") == "flv") tracks = 1;
    }
    result.video =
        share ? shareVideo(frontend, "simple_video_recording", videoId, settings.p) : createVideo(videoId, settings.p);
    result.folder = std::filesystem::u8path(get(config, "SimpleOutput", "FilePath"));
  }
  std::ostringstream summary;
  summary << obs_encoder_get_display_name(obs_encoder_get_id(result.video)) << " ("
          << obs_encoder_get_codec(result.video) << ")\n";
  summary << obs_encoder_get_width(result.video) << "×" << obs_encoder_get_height(result.video) << " at "
          << result.videoInfo.fps_num << '/' << result.videoInfo.fps_den << " fps\nAudio: ";
  for (int i = 0; i < 6; ++i) {
    if (!(tracks & (1 << i))) continue;
    auto name = get(config, "AdvOut", ("Track" + std::to_string(i + 1) + "Name").c_str());
    if (name.empty()) name = "Track " + std::to_string(i + 1);
    int trackBitrate =
        advanced ? int(config_get_int(config, "AdvOut", ("Track" + std::to_string(i + 1) + "Bitrate").c_str()))
                 : bitrate;
    if (share) {
      // Frontend creation names: per-track encoders, or one FLV encoder in Simple mode.
      std::string frontendName = "adv_record_audio_" + std::to_string(i);
      if (!advanced) {
        frontendName =
            get(config, "SimpleOutput", "RecAudioEncoder") == "opus" ? "simple_opus_recording" : "simple_aac_recording";
        if (get(config, "SimpleOutput", "RecFormat2") != "flv") frontendName += std::to_string(i);
      }
      Data data;
      obs_data_set_int(data.p, "bitrate", closestBitrate(audioId, trackBitrate));
      if (!advanced) obs_data_set_string(data.p, "rate_control", "CBR");
      result.audio.push_back(shareAudio(frontend, result.audio.size(), frontendName, audioId, i, data.p));
    } else
      result.audio.push_back(createAudio(audioId, name, i, trackBitrate));
    result.trackNames.push_back(name);
    summary << (result.audio.size() > 1 ? ", " : "") << (i + 1) << ' ' << name;
  }
  if (result.audio.empty()) summary << "video only";
  if (share)
    summary << "\nShared with OBS recording: one video encoding. Pausing the recording pauses replay collection.";
  else
    summary << "\nIndependent replay encoder; simultaneous recording uses an additional video encoding.";
  result.shared = share;
  result.description = summary.str();
  result.fingerprint = recordingFingerprint();
  return result;
}
}
Encoders recordingEncoders(bool share)
{
  if (!share) return build(false);
  try {
    return build(true);
  } catch (const ShareUnavailable &e) {
    auto result = build(false);
    result.shareNote = e.what();
    return result;
  }
}
obs_encoder_t *frontendRecordingVideo()
{
  auto *config = obs_frontend_get_profile_config();
  if (!config) return nullptr;
  Frontend frontend;
  return frontend.video(get(config, "Output", "Mode") == "Advanced" ? "advanced_video_recording"
                                                                    : "simple_video_recording");
}
}
