#include "capture.hpp"
#include <util/platform.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace replay;
namespace {
#ifndef _WIN32
std::filesystem::path obsPrefix()
{
  const char *root = std::getenv("OBS_ROOT");
  return root ? std::filesystem::u8path(root) : std::filesystem::path("/usr");
}
std::filesystem::path obsLib()
{
  auto prefix = obsPrefix();
  return prefix / (std::filesystem::exists(prefix / "lib64/libobs-opengl.so") ? "lib64" : "lib");
}
#endif
const char *audioName(void *) { return "Replay probe tone"; }
void *audioCreate(obs_data_t *, obs_source_t *) { return new int(0); }
void audioDestroy(void *p) { delete static_cast<int *>(p); }
void loadModule(const char *name)
{
  obs_module_t *module = nullptr;
#ifdef _WIN32
  const char *root = std::getenv("OBS_ROOT");
  std::filesystem::path base =
      root ? std::filesystem::u8path(root) : std::filesystem::path(L"C:/Program Files/obs-studio");
  auto path = (base / "obs-plugins/64bit" / (std::string(name) + ".dll")).u8string();
  auto data = (base / "data/obs-plugins" / name).u8string();
#else
  auto path = (obsLib() / "obs-plugins" / (std::string(name) + ".so")).string();
  auto data = (obsPrefix() / "share/obs/obs-plugins" / name).string();
#endif
  if (obs_open_module(&module, path.c_str(), data.c_str()) == MODULE_SUCCESS) obs_init_module(module);
}
void sleepPoll(Capture &capture, int ms)
{
  for (int i = 0; i < ms; i += 20) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    capture.poll();
  }
}
}
int main(int argc, char **argv)
{
  if (argc < 3) {
    std::cerr << "Usage: obs_probe WRITER DESTINATION [av1|hevc|h264|x264] [full]\n";
    return 2;
  }
  const std::string codec = argc > 3 ? argv[3] : "av1";
  bool full = argc > 4 && std::string(argv[4]) == "full";
  bool longRun = argc > 5 && std::string(argv[5]) == "long";
  bool pressureRun = argc > 5 && std::string(argv[5]) == "pressure";
  bool videoOnly = argc > 5 && std::string(argv[5]) == "video-only";
  bool sharedRun = argc > 5 && std::string(argv[5]) == "shared";
  const char *audioChoice = std::getenv("REPLAY_PROBE_AUDIO");
  std::string audioEncoder = std::string("ffmpeg_") + (audioChoice ? audioChoice : "flac");
  setWriterPath(std::filesystem::absolute(argv[1]));
  std::filesystem::create_directories(argv[2]);
  if (!obs_startup("en-US", nullptr, nullptr)) return 3;
  int result = 1;
  try {
    obs_audio_info ai{};
    ai.samples_per_sec = 48000;
    ai.speakers = SPEAKERS_STEREO;
    if (!obs_reset_audio(&ai)) throw std::runtime_error("Audio initialization failed");
    obs_video_info vi{};
#ifdef _WIN32
    const char *root = std::getenv("OBS_ROOT");
    std::filesystem::path base =
        root ? std::filesystem::u8path(root) : std::filesystem::path(L"C:/Program Files/obs-studio");
    auto graphics = (base / "bin/64bit/libobs-d3d11.dll").u8string();
    vi.graphics_module = graphics.c_str();
    obs_add_data_path((base / "data/libobs").u8string().c_str());
#else
    auto graphics = (obsLib() / "libobs-opengl.so").string();
    vi.graphics_module = graphics.c_str();
    obs_add_data_path((obsPrefix() / "share/obs/libobs").string().c_str());
#endif
    vi.fps_num = 60;
    vi.fps_den = 1;
    vi.base_width = vi.output_width = full ? 3840 : 640;
    vi.base_height = vi.output_height = full ? 2160 : 360;
    vi.output_format = VIDEO_FORMAT_NV12;
    vi.colorspace = VIDEO_CS_709;
    vi.range = VIDEO_RANGE_PARTIAL;
    vi.gpu_conversion = true;
    if (obs_reset_video(&vi) != OBS_VIDEO_SUCCESS) throw std::runtime_error("Video initialization failed");
    loadModule("obs-nvenc");
    loadModule("obs-ffmpeg");
    loadModule("obs-x264");
    loadModule("image-source");
    obs_post_load_modules();
    registerReplayOutput();
    obs_source_info info{};
    info.id = "multi_replay_probe_tone";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = audioName;
    info.create = audioCreate;
    info.destroy = audioDestroy;
    obs_register_source(&info);
    auto *audioSource = obs_source_create(info.id, "Probe audio", nullptr, nullptr);
    obs_source_set_audio_mixers(audioSource, 15);
    obs_set_output_source(1, audioSource);
    auto *color = obs_data_create();
    obs_data_set_int(color, "color", 0xff4080d0);
    obs_data_set_int(color, "width", vi.output_width);
    obs_data_set_int(color, "height", vi.output_height);
    auto *videoSource = obs_source_create("color_source_v3", "Probe video", color, nullptr);
    obs_data_release(color);
    obs_set_output_source(0, videoSource);
    std::atomic<bool> audioRunning{true};
    std::thread audioThread([&] {
      float tone[2][1024];
      uint64_t sample = 0;
      uint64_t start = os_gettime_ns();
      while (audioRunning) {
        for (int i = 0; i < 1024; ++i)
          tone[0][i] = tone[1][i] = float(std::sin(double(sample + i) * 440 * 6.283185307179586 / 48000) * 0.1);
        obs_source_audio block{};
        block.data[0] = reinterpret_cast<uint8_t *>(tone[0]);
        block.data[1] = reinterpret_cast<uint8_t *>(tone[1]);
        block.frames = 1024;
        block.speakers = SPEAKERS_STEREO;
        block.samples_per_sec = 48000;
        block.format = AUDIO_FORMAT_FLOAT_PLANAR;
        block.timestamp = start + sample * 1000000000ULL / 48000;
        obs_source_output_audio(audioSource, &block);
        sample += 1024;
        os_sleepto_ns(start + sample * 1000000000ULL / 48000);
      }
    });
    try {
      auto *settings = obs_data_create();
      obs_data_set_string(settings, "rate_control", codec == "x264" ? "CRF" : "CQP");
      obs_data_set_int(settings, "cqp", 24);
      obs_data_set_int(settings, "crf", 24);
      obs_data_set_string(settings, "preset", codec == "x264" ? "veryfast" : "p4");
      obs_data_set_string(settings, "multipass", "disabled");
      obs_data_set_int(settings, "keyint_sec", 2);
      std::string id = codec == "x264" ? "obs_x264" : "obs_nvenc_" + codec + "_tex";
      auto *video = obs_video_encoder_create(id.c_str(), "Replay prototype video", settings, nullptr);
      auto *normalVideo = obs_video_encoder_create(id.c_str(), "Ordinary recording video", settings, nullptr);
      obs_data_release(settings);
      if (!video || !normalVideo) throw std::runtime_error("Configured video encoder is unavailable");
      obs_encoder_set_video(video, obs_get_video());
      obs_encoder_set_video(normalVideo, obs_get_video());
      std::vector<obs_encoder_t *> audio, normalAudio;
      for (int i = 0; i < 4; ++i) {
        const char *names[] = {"Game", "Mic", "Discord", "Desktop"};
        auto *encoder = obs_audio_encoder_create(audioEncoder.c_str(), names[i], nullptr, i, nullptr);
        auto *normal = obs_audio_encoder_create(audioEncoder.c_str(), (std::string("Normal ") + names[i]).c_str(),
                                                nullptr, i, nullptr);
        if (!encoder || !normal) throw std::runtime_error("FLAC encoder unavailable");
        obs_encoder_set_audio(encoder, obs_get_audio());
        obs_encoder_set_audio(normal, obs_get_audio());
        audio.push_back(encoder);
        normalAudio.push_back(normal);
      }
      auto replayAudio = videoOnly ? std::vector<obs_encoder_t *>{} : audio;
      auto *normalSettings = obs_data_create();
      auto normalPath = std::filesystem::path(argv[2]) / ("normal-" + codec + ".mkv");
      obs_data_set_string(normalSettings, "path", normalPath.u8string().c_str());
      auto *normalOutput = obs_output_create("ffmpeg_muxer", "Ordinary recording prototype", normalSettings, nullptr);
      obs_data_release(normalSettings);
      obs_output_set_video_encoder(normalOutput, normalVideo);
      for (size_t i = 0; i < normalAudio.size(); ++i) obs_output_set_audio_encoder(normalOutput, normalAudio[i], i);
      if (sharedRun) {
        // normalOutput stands in for the frontend's recording; replay borrows its encoders.
        auto expect = [](bool ok, const char *what) {
          if (!ok) throw std::runtime_error(what);
        };
        auto waitStopped = [](obs_output_t *output) {
          for (int i = 0; i < 500 && obs_output_active(output); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        };
        Capture capture;
        Options options;
        options.folder = argv[2];
        options.budget = 256000000;
        options.longest = 600000000;
        options.shared = true;
        options.trackNames = {"Game", "Mic", "Discord", "Desktop"};
        // Replay first: it starts the recording's encoders; the recording joins them.
        capture.start(options, normalVideo, normalAudio);
        sleepPoll(capture, 3500);
        expect(!obs_encoder_active(video), "A second video encoder ran in shared mode");
        capture.save(60000000, clockNow());
        if (!obs_output_start(normalOutput)) throw std::runtime_error("Recording could not join the shared encoders");
        sleepPoll(capture, 3000);
        capture.save(2000000, clockNow());
        sleepPoll(capture, 1000);
        // Recording pause stops the shared encoders: replay freezes instead of failing.
        expect(obs_output_pause(normalOutput, true), "Recording pause failed");
        capture.notePause(true);
        sleepPoll(capture, 500);
        auto before = capture.status();
        sleepPoll(capture, 6000);
        auto during = capture.status();
        expect(during.collecting && during.paused,
               "Replay did not survive a recording pause longer than its stall limit");
        expect(during.available <= before.available + 100000, "History grew while the shared encoder was paused");
        capture.save(2000000, clockNow());
        expect(obs_output_pause(normalOutput, false), "Recording resume failed");
        capture.notePause(false);
        sleepPoll(capture, 2500);
        expect(capture.status().collecting && !capture.status().paused, "Replay did not resume with the recording");
        capture.save(4000000, clockNow()); // Spans the pause; the paused time must be cut out.
        // Recording stops: the encoders keep running for replay.
        obs_output_stop(normalOutput);
        waitStopped(normalOutput);
        auto afterStop = capture.status().available;
        sleepPoll(capture, 1500);
        expect(capture.status().collecting && capture.status().available > afterStop + 1000000,
               "Stopping the recording stopped shared replay");
        expect(obs_encoder_active(normalVideo), "Shared encoder stopped with the recording");
        capture.save(2000000, clockNow());
        sleepPoll(capture, 1000);
        capture.stopAndWait();
        expect(!obs_encoder_active(normalVideo) && !obs_video_active(),
               "Encoders were still attached after stopAndWait");
        for (int i = 0; i < 500 && capture.status().draining; ++i) sleepPoll(capture, 20);
        // Recording first: replay joins the running encoders and leaves them running.
        auto *secondSettings = obs_data_create();
        obs_data_set_string(secondSettings, "path",
                            (std::filesystem::path(argv[2]) / ("normal-" + codec + "-second.mkv")).u8string().c_str());
        auto *second = obs_output_create("ffmpeg_muxer", "Ordinary recording prototype 2", secondSettings, nullptr);
        obs_data_release(secondSettings);
        obs_output_set_video_encoder(second, normalVideo);
        for (size_t i = 0; i < normalAudio.size(); ++i) obs_output_set_audio_encoder(second, normalAudio[i], i);
        if (!obs_output_start(second)) throw std::runtime_error("Second recording could not start");
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        capture.start(options, normalVideo, normalAudio);
        sleepPoll(capture, 4500);
        capture.save(2000000, clockNow());
        sleepPoll(capture, 1000);
        capture.stopAndWait();
        expect(obs_output_active(second) && obs_encoder_active(normalVideo),
               "Stopping shared replay stopped the recording");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        obs_output_stop(second);
        waitStopped(second);
        obs_output_release(second);
        capture.shutdown();
        auto status = capture.status();
        if (status.results.size() != 6 || status.memory != 0)
          throw std::runtime_error("Drain lost requests or retained memory");
        for (size_t i = 0; i < status.results.size(); ++i) {
          auto &saved = status.results[i];
          if (!saved.error.empty()) throw std::runtime_error(saved.error);
          // The save made during the pause ends at the last packet libobs released
          // before pausing, which is earlier than the request.
          if (i != 2 && std::abs(saved.mediaEnd - saved.endpoint) > 100000)
            throw std::runtime_error("Shared-mode endpoint exceeded the 100 ms tolerance");
          if (i > 0 &&
              (!saved.partial.empty() || saved.actual < saved.requested || saved.actual > saved.requested + 2500000))
            throw std::runtime_error("Shared-mode clip duration is wrong: " + saved.partial);
          std::cout << "SAVED\t" << saved.path.u8string() << '\t' << saved.requested << '\t' << saved.actual << '\t'
                    << saved.endpoint << '\t' << saved.partial << "\tend_delta_us=" << saved.mediaEnd - saved.endpoint
                    << '\n';
        }
      } else {
        Capture capture;
        Options options;
        options.folder = argv[2];
        options.budget = 256000000;
        options.longest = 600000000;
        capture.start(options, video, replayAudio);
        sleepPoll(capture, 3500);
        capture.save(60000000, clockNow()); // Warmup and configured default duration.
        if (!obs_output_start(normalOutput)) throw std::runtime_error("Normal recording could not start beside replay");
        sleepPoll(capture, 2500);
        const Time firstEndpoint = clockNow();
        capture.save(2000000, firstEndpoint);
        capture.save(4000000, clockNow());
        capture.save(2000000, clockNow());
        if (!obs_output_pause(normalOutput, true)) throw std::runtime_error("Recording pause failed");
        auto before = capture.status().available;
        sleepPoll(capture, 1500);
        auto after = capture.status().available;
        if (after <= before + 1000000) throw std::runtime_error("Replay did not continue during recording pause");
        obs_output_pause(normalOutput, false);
        capture.save(2000000, clockNow());
        sleepPoll(capture, 1000);
        capture.stop();
        for (int i = 0; i < 500 && capture.status().draining; ++i) sleepPoll(capture, 20);
        auto first = capture.status();
        if (first.results.size() != 5) throw std::runtime_error("Accepted request count changed");
        for (auto &saved : first.results)
          if (!saved.error.empty()) throw std::runtime_error(saved.error);
        if (first.results[1].endpoint != firstEndpoint) throw std::runtime_error("Request endpoint shifted");
        if (!obs_output_active(normalOutput)) throw std::runtime_error("Stopping replay stopped ordinary recording");
        capture.start(options, video, replayAudio); // Recording already running.
        sleepPoll(capture, 2500);
        obs_output_force_stop(normalOutput);
        sleepPoll(capture, 500);
        if (!capture.status().collecting) throw std::runtime_error("Stopping recording stopped replay");
        capture.save(2000000, clockNow());
        capture.save(240000000, clockNow());
        capture.save(600000000, clockNow());
        capture.stop();
        for (int i = 0; i < 500 && capture.status().draining; ++i) sleepPoll(capture, 20);
        if (longRun) {
          options.budget = 16000000000ULL;
          capture.start(options, video, replayAudio);
          for (int seconds = 0; seconds < 610; ++seconds) {
            sleepPoll(capture, 1000);
            if (!capture.status().collecting) throw std::runtime_error("Long replay collection stopped");
            if (seconds % 30 == 0)
              std::cout << "LONG RUN " << seconds << " s, history " << capture.status().available / 1e6 << " s, memory "
                        << capture.status().memory << std::endl;
          }
          for (auto duration : {60000000LL, 240000000LL, 600000000LL}) capture.save(duration, clockNow());
          sleepPoll(capture, 1000);
          capture.stop();
        }
        if (pressureRun) {
          options.budget = writerReserve + 1572864;
          capture.start(options, video, replayAudio);
          for (int i = 0; i < 1500 && !capture.status().degraded; ++i) sleepPoll(capture, 20);
          auto pressure = capture.status();
          if (!pressure.collecting || !pressure.degraded || pressure.memory > options.budget)
            throw std::runtime_error("Pressure did not shorten valid history within budget");
          // The save pins the whole shortened history; collection must outlive it.
          capture.save(60000000, clockNow());
          sleepPoll(capture, 4000);
          if (!capture.status().collecting)
            throw std::runtime_error("Saving under memory pressure stopped collection: " + capture.status().message);
          capture.stop();
          for (int i = 0; i < 500 && capture.status().draining; ++i) sleepPoll(capture, 20);
          if (capture.status().results.back().partial != "memory pressure")
            throw std::runtime_error("Pressure-shortened replay was not marked partial");
        }
        capture.shutdown();
        auto status = capture.status();
        if (status.results.size() != size_t(longRun ? 11 : pressureRun ? 9 : 8) || status.memory != 0)
          throw std::runtime_error("Drain lost requests or retained memory");
        if (longRun)
          for (size_t i = 8; i < 11; ++i) {
            auto &saved = status.results[i];
            if (!saved.partial.empty() || saved.actual < saved.requested || saved.actual > saved.requested + 2500000)
              throw std::runtime_error("Long replay duration/keyframe guarantee failed");
          }
        for (auto &saved : status.results) {
          if (!saved.error.empty()) throw std::runtime_error(saved.error);
          if (saved.partial.find("settled") == std::string::npos &&
              saved.partial.find("timeout") == std::string::npos && std::abs(saved.mediaEnd - saved.endpoint) > 100000)
            throw std::runtime_error("Settled video endpoint exceeded the clean-probe 100 ms tolerance");
          std::cout << "SAVED\t" << saved.path.u8string() << '\t' << saved.requested << '\t' << saved.actual << '\t'
                    << saved.endpoint << '\t' << saved.partial << "\tend_delta_us=" << saved.mediaEnd - saved.endpoint
                    << '\n';
        }
      }
      obs_output_release(normalOutput);
      obs_encoder_release(video);
      obs_encoder_release(normalVideo);
      for (auto *e : audio) obs_encoder_release(e);
      for (auto *e : normalAudio) obs_encoder_release(e);
      result = 0;
    } catch (...) {
      audioRunning = false;
      audioThread.join();
      throw;
    }
    audioRunning = false;
    audioThread.join();
    obs_set_output_source(0, nullptr);
    obs_set_output_source(1, nullptr);
    obs_source_release(videoSource);
    obs_source_release(audioSource);
  } catch (const std::exception &e) {
    std::cerr << "PROBE FAILED: " << e.what() << '\n';
  }
  obs_shutdown();
  return result;
}
