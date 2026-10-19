#include "capture.hpp"
#include "wire.hpp"
#include <util/platform.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
extern "C" {
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
}

namespace replay {
Time clockNow() { return Time(os_gettime_ns() / 1000); }
namespace {
struct OutputContext {
  obs_output_t *output;
  Capture *capture = nullptr;
};
void *createOutput(obs_data_t *settings, obs_output_t *output)
{
  // Transient private settings, created and consumed within this process only.
  auto *capture = reinterpret_cast<Capture *>(intptr_t(obs_data_get_int(settings, "capture_context")));
  return new OutputContext{output, capture};
}
void destroyOutput(void *data) { delete static_cast<OutputContext *>(data); }
bool startOutput(void *data)
{
  auto *ctx = static_cast<OutputContext *>(data);
  if (!ctx->capture || !obs_output_can_begin_data_capture(ctx->output, 0) ||
      !obs_output_initialize_encoders(ctx->output, 0))
    return false;
  if (!ctx->capture->begin(ctx->output)) return false;
  return obs_output_begin_data_capture(ctx->output, 0);
}
void stopOutput(void *data, uint64_t)
{
  auto *ctx = static_cast<OutputContext *>(data);
  obs_output_end_data_capture(ctx->output);
  if (ctx->capture) ctx->capture->end();
}
void receivePacket(void *data, encoder_packet *packet)
{
  auto *ctx = static_cast<OutputContext *>(data);
  if (ctx->capture) ctx->capture->receive(packet);
}
const char *outputName(void *) { return "Multi-Length Replay packet history"; }
struct OwnedPacket {
  encoder_packet packet{};
  std::vector<uint8_t> copied;
  bool referenced = false;
  explicit OwnedPacket(encoder_packet *p, bool videoOnly)
  {
    if (videoOnly) {
      // libobs's VIDEO-only callback exposes encoder scratch memory without a
      // packet reference header. Copy ONCE into shared immutable replay storage.
      packet = *p;
      copied.assign(p->data, p->data + p->size);
      packet.data = copied.data();
    } else {
      obs_encoder_packet_ref(&packet, p);
      referenced = true;
    }
  }
  ~OwnedPacket()
  {
    if (referenced) obs_encoder_packet_release(&packet);
  }
};
Stream describe(obs_encoder_t *encoder, bool video)
{
  Stream s;
  s.codec = obs_encoder_get_codec(encoder);
  s.name = obs_encoder_get_name(encoder);
  uint8_t *extra = nullptr;
  size_t size = 0;
  if (obs_encoder_get_extra_data(encoder, &extra, &size) && size) s.extra.assign(extra, extra + size);
  if (video) {
    s.width = int(obs_encoder_get_width(encoder));
    s.height = int(obs_encoder_get_height(encoder));
    obs_video_info vi{};
    obs_get_video_info(&vi);
    s.fpsNum = int(vi.fps_num);
    s.fpsDen = int(vi.fps_den) * int(obs_encoder_get_frame_rate_divisor(encoder));
    auto cs = obs_encoder_get_preferred_color_space(encoder);
    if (cs == VIDEO_CS_DEFAULT) cs = vi.colorspace;
    switch (cs) {
    case VIDEO_CS_601:
      s.primaries = AVCOL_PRI_SMPTE170M;
      s.transfer = AVCOL_TRC_SMPTE170M;
      s.colorspace = AVCOL_SPC_SMPTE170M;
      break;
    case VIDEO_CS_SRGB: s.transfer = AVCOL_TRC_IEC61966_2_1; break;
    case VIDEO_CS_2100_PQ:
      s.primaries = AVCOL_PRI_BT2020;
      s.transfer = AVCOL_TRC_SMPTE2084;
      s.colorspace = AVCOL_SPC_BT2020_NCL;
      s.maxLuminance = int(obs_get_video_hdr_nominal_peak_level());
      break;
    case VIDEO_CS_2100_HLG:
      s.primaries = AVCOL_PRI_BT2020;
      s.transfer = AVCOL_TRC_ARIB_STD_B67;
      s.colorspace = AVCOL_SPC_BT2020_NCL;
      s.maxLuminance = 1000;
      break;
    default: break;
    }
    auto range = obs_encoder_get_preferred_range(encoder);
    if (range == VIDEO_RANGE_DEFAULT) range = vi.range;
    s.range = range == VIDEO_RANGE_FULL ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
  } else {
    auto *ai = audio_output_get_info(obs_get_audio());
    s.sampleRate = int(obs_encoder_get_sample_rate(encoder));
    s.channels = int(get_audio_channels(ai->speakers));
    s.frameSize = int(obs_encoder_get_frame_size(encoder));
    s.priming = int(obs_encoder_get_priming_samples(encoder));
  }
  return s;
}
std::string timestampName(Time endpoint = 0)
{
  auto now = std::chrono::system_clock::now();
  if (endpoint) now += std::chrono::microseconds(endpoint - clockNow());
  auto time = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &time);
#else
  localtime_r(&time, &local);
#endif
  std::ostringstream value;
  value << std::put_time(&local, "%Y-%m-%d_%H-%M-%S");
  return value.str();
}
std::string sessionName()
{
  std::ostringstream value;
  value << timestampName() << '_' << std::hex << std::random_device{}();
  return value.str();
}
}
std::shared_ptr<Format> Capture::describeOutput(obs_output_t *output) const
{
  auto format = std::make_shared<Format>();
  format->streams.push_back(describe(obs_output_get_video_encoder(output), true));
  for (size_t i = 0; i < 6; ++i) {
    auto *encoder = obs_output_get_audio_encoder(output, i);
    if (!encoder) break;
    format->streams.push_back(describe(encoder, false));
    // Shared frontend encoders carry internal names until the frontend renames them.
    if (i < options_.trackNames.size()) format->streams.back().name = options_.trackNames[i];
  }
  return format;
}
void registerReplayOutput()
{
  obs_output_info info{};
  info.id = "multi_length_replay";
  info.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED | OBS_OUTPUT_MULTI_TRACK;
  info.get_name = outputName;
  info.create = createOutput;
  info.destroy = destroyOutput;
  info.start = startOutput;
  info.stop = stopOutput;
  info.encoded_packet = receivePacket;
  info.encoded_video_codecs = "h264;hevc;av1";
  info.encoded_audio_codecs = "aac;flac;opus;pcm_s16le;pcm_s24le;pcm_f32le";
  obs_register_output(&info);
  info.id = "multi_length_replay_video";
  info.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED;
  obs_register_output(&info);
}
Capture::Capture() { worker_ = std::thread(&Capture::worker, this); }
Capture::~Capture() { shutdown(); }
void Capture::start(const Options &options, obs_encoder_t *video, const std::vector<obs_encoder_t *> &audio)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (collecting_ || captureActive_ || writing_ || !pending_.empty() || !ready_.empty())
      throw std::runtime_error("Buffer is active or draining; wait before starting again");
    if (options.budget < writerReserve + 1024 * 1024)
      throw std::runtime_error("RAM budget must exceed writer reserve plus 1 MiB");
    options_ = options;
    obs_video_info namingInfo{};
    obs_get_video_info(&namingInfo);
    namingVideo_ = {namingInfo.fps_num,
                    namingInfo.fps_den,
                    namingInfo.base_width,
                    namingInfo.base_height,
                    namingInfo.output_width,
                    namingInfo.output_height,
                    get_video_format_name(namingInfo.output_format)};
    ledger_ = std::make_shared<Ledger>();
    ledger_->limit = options.budget;
    ledger_->used = writerReserve;
    history_ = std::make_unique<History>(ledger_, options.longest);
    watermark_.fill(0);
    rawMark_.fill(0);
    shift_.fill(0);
    step_.fill(0);
    paused_ = resumeNoted_ = false;
    // An encoder that is already running sent its priming packet to someone else.
    primed_.fill(false);
    for (size_t i = 0; i < audio.size() && i < 6; ++i) primed_[i + 1] = !obs_encoder_active(audio[i]);
    session_ = sessionName();
    message_ = "Starting";
    needsStop_ = false;
    abortWrites_ = false;
    headersReady_ = false;
    stopRequested_ = false;
  }
  if (output_) {
    obs_output_release(output_);
    output_ = nullptr;
  }
  auto *settings = obs_data_create();
  obs_data_set_int(settings, "capture_context", int64_t(reinterpret_cast<intptr_t>(this)));
  output_ = obs_output_create(audio.empty() ? "multi_length_replay_video" : "multi_length_replay",
                              "Multi-Length Replay", settings, nullptr);
  obs_data_release(settings);
  if (!output_) throw std::runtime_error("Cannot create replay packet output");
  obs_output_set_video_encoder(output_, video);
  for (size_t i = 0; i < audio.size(); ++i) obs_output_set_audio_encoder(output_, audio[i], i);
  if (!obs_output_start(output_)) {
    std::lock_guard<std::mutex> lock(mutex_);
    collecting_ = captureActive_ = false;
    history_->clear();
    ledger_->used = 0;
    const char *error = obs_output_get_last_error(output_);
    message_ = error && *error ? error : "Replay encoder initialization failed; inspect OBS log";
    throw std::runtime_error(message_);
  }
}
bool Capture::begin(obs_output_t *output)
{
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    auto format = describeOutput(output);
    format_ = format;
    collecting_ = captureActive_ = true;
    lastPacket_ = clockNow();
    lastPoll_ = 0;
    lastWall_ = 0;
    message_ = "Buffering";
    if (options_.shared)
      blog(LOG_INFO,
           "[multi-replay] Session %s: SHARING the OBS recording's %s encoder '%s', %zu audio tracks; no additional "
           "video encoding",
           session_.c_str(), format->streams[0].codec.c_str(),
           obs_encoder_get_name(obs_output_get_video_encoder(output)), format->streams.size() - 1);
    else
      blog(LOG_INFO,
           "[multi-replay] Session %s: ONE independent %s encoder, %zu audio tracks; normal recording remains separate",
           session_.c_str(), format->streams[0].codec.c_str(), format->streams.size() - 1);
    return true;
  } catch (const std::exception &e) {
    obs_output_set_last_error(output, e.what());
    return false;
  }
}
void Capture::stop()
{
  bool stopNow;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    collecting_ = false;
    if (!stopRequested_) {
      stopRequested_ = true;
      stopDeadline_ = clockNow() + 2000000;
      // Accepted snapshots own their pins. Unsaved rolling history is no
      // longer needed; delayed delivery is retained only for accepted jobs.
      if (history_) history_->clear();
    }
    if (message_ == "Buffering") message_ = "Draining accepted saves";
    stopNow = needsStop_ || pending_.empty() || clockNow() >= stopDeadline_;
  }
  if (stopNow) {
    if (output_ && obs_output_active(output_))
      obs_output_force_stop(output_);
    else
      end();
  }
}
void Capture::end()
{
  std::lock_guard<std::mutex> lock(mutex_);
  collecting_ = captureActive_ = needsStop_ = stopRequested_ = paused_ = resumeNoted_ = false;
  // Keep accepted jobs; libobs has stopped delivering, so settle available data.
  settle();
  finishIfDrained();
  condition_.notify_all();
}
void Capture::failCapture(std::string error)
{
  collecting_ = false;
  needsStop_ = true;
  message_ = std::move(error);
  blog(LOG_ERROR, "[multi-replay] %s", message_.c_str());
  condition_.notify_all();
}
void Capture::receive(encoder_packet *packet)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!packet) {
    failCapture("Encoder failed; accepted saves will drain");
    return;
  }
  if (!collecting_ && !stopRequested_) return;
  try {
    int track = packet->type == OBS_ENCODER_VIDEO ? 0 : int(packet->track_idx) + 1;
    if (!headersReady_ && !track) {
      // NVENC produces codec extradata only after its first encoded frame.
      // Freeze the format here, before any playable history can be saved.
      format_ = describeOutput(output_);
      headersReady_ = true;
    }
    if (!format_ || track >= int(format_->streams.size()) || packet->timebase_den <= 0) {
      failCapture("Encoded stream configuration changed");
      return;
    }
    AVRational base{packet->timebase_num, packet->timebase_den};
    auto &s = format_->streams[track];
    const Time duration = track ? av_rescale_q(s.frameSize, {1, s.sampleRate}, {1, 1000000})
                                : av_rescale_q(1, {s.fpsDen, s.fpsNum}, {1, 1000000});
    const Time raw = packet->sys_dts_usec;
    const bool firstPacket = rawMark_[track] == 0;
    if (!firstPacket) {
      const Time delta = raw - rawMark_[track];
      if (delta < 0 || (!options_.shared && delta > 5000000)) {
        failCapture("Replay timestamp discontinuity; manual restart required");
        return;
      }
      // A track's packets are evenly spaced in system time unless its encoder
      // was paused, which only the recording that owns shared encoders can do.
      // The spacing is observed rather than taken from encoder metadata.
      if (!step_[track] || delta < step_[track])
        step_[track] = delta;
      else if (options_.shared && delta - step_[track] > 1000) {
        shift_[track] += delta - step_[track];
        if (!track)
          blog(LOG_INFO, "[multi-replay] Shared encoder paused for %.3f s; that time is cut from the replay timeline",
               (delta - step_[track]) / 1e6);
      }
    }
    rawMark_[track] = raw;
    lastPacket_ = clockNow();
    if (!track && paused_ && resumeNoted_ && raw >= resumeWall_) paused_ = resumeNoted_ = false;
    const Time dts = raw - shift_[track];
    const Time pts = dts + av_rescale_q(packet->pts - packet->dts, base, {1, 1000000});
    watermark_[track] = dts;
    bool needed = collecting_;
    for (auto &job : pending_) needed |= dts <= job.clip.requestedEnd + (track ? 0 : settleWindow);
    if (!needed) {
      settle();
      return;
    }
    if (packet->size > maxPacketSize) {
      failCapture("Encoded frame exceeds the bounded writer's 8 MiB packet limit");
      return;
    }
    if (!history_->prepare(packet->size)) {
      failCapture("RAM budget exhausted by protected saves or the minimum playable GOP; draining");
      return;
    }
    auto owner = std::make_shared<OwnedPacket>(packet, format_->streams.size() == 1);
    Packet p;
    p.owner = owner;
    p.data = owner->packet.data;
    p.size = packet->size;
    p.dts = dts;
    p.pts = pts;
    p.track = track;
    p.key = packet->keyframe;
    p.priming = track && firstPacket && primed_[track] ? s.priming : 0;
    p.duration = duration;
    auto retained = history_->retain(std::move(p));
    if (collecting_) history_->push(retained);
    for (auto &job : pending_) {
      bool afterStart =
          retained->track ? retained->pts + retained->duration > job.clip.start : retained->dts >= job.clip.decodeStart;
      if (afterStart && retained->dts <= job.clip.requestedEnd + (retained->track ? 0 : settleWindow)) {
        if (job.clip.packets.size() >= job.capacity) {
          failCapture("Packet settlement exceeded reserved job capacity");
          break;
        }
        job.clip.packets.push_back(retained);
      }
    }
    settle();
  } catch (const std::exception &e) {
    failCapture(std::string("Replay capture failed: ") + e.what());
  }
}
uint64_t Capture::save(Time duration, Time endpoint, const std::filesystem::path &destination)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!collecting_) throw std::runtime_error("Replay buffer is stopped or draining");
  if (duration <= 0 || duration > options_.longest)
    throw std::runtime_error("Preset duration is outside this session's configured history");
  if (pending_.size() + ready_.size() + size_t(writing_) >= 128)
    throw std::runtime_error("Save queue reached its 128-request safety limit");
  const size_t capacity = history_->count() + 4096;
  const uint64_t bytes = jobOverhead + uint64_t(capacity) * refOverhead;
  // This save may pin the whole history. Under memory pressure, shorten the
  // unpinned history now so packets still fit until the writer releases pins.
  history_->reserve(bytes + history_->recentBytes(settleReserve));
  if (bytes > ledger_->limit || !history_->prepare(size_t(bytes)))
    throw std::runtime_error("Not enough replay memory to safely accept another save");
  Job job;
  // While a shared recording is paused nothing newer than the pause can arrive.
  job.frozen = paused_;
  job.clip = history_->select(paused_ ? pauseEnd_ : endpoint - shift_[0], duration);
  job.clip.packets.reserve(capacity);
  job.folder = destination.empty() ? options_.folder : destination;
  job.capacity = capacity;
  job.metadata = bytes;
  job.accepted = clockNow();
  const auto requestWall = std::chrono::system_clock::now() + std::chrono::microseconds(endpoint - clockNow());
  job.stem = replayFilename(options_.filenames, namingVideo_, std::chrono::system_clock::to_time_t(requestWall),
                            duration / 1000000);
  job.id = ++sequence_;
  job.allowSpaces = options_.filenames.allowSpaces;
  pending_.push_back(std::move(job));
  ledger_->used += bytes;
  settle();
  condition_.notify_all();
  return sequence_;
}
void Capture::settle()
{
  while (!pending_.empty()) {
    auto &job = pending_.front();
    bool passed = true;
    for (size_t i = 0; !job.frozen && i < format_->streams.size(); ++i)
      passed &= watermark_[i] > job.clip.requestedEnd + settleWindow;
    bool timeout = clockNow() - job.accepted >= 2000000;
    if (captureActive_ && !passed && !timeout) break;
    if (!passed && (timeout || !captureActive_)) {
      if (!job.clip.partial.empty()) job.clip.partial += "; ";
      job.clip.partial += !captureActive_ ? "capture stopped before endpoint settled"
                          : paused_       ? "recording paused before endpoint settled"
                                          : "encoder settlement timeout";
    }
    finalizeSelection(job.clip);
    if (!job.clip.partial.empty()) job.stem += job.allowSpaces ? " partial" : "_partial";
    ready_.push_back(std::move(job));
    pending_.pop_front();
    condition_.notify_all();
  }
}
void Capture::finishIfDrained()
{
  if (!captureActive_ && !collecting_ && pending_.empty() && ready_.empty() && !writing_ && history_) {
    history_->clear();
    // No packet/job allocations survive. The worker reserve is inactive.
    ledger_->used = 0;
    if (message_ == "Buffering" || message_ == "Draining accepted saves") message_ = "Stopped";
    condition_.notify_all();
  }
}
void Capture::record(Result result)
{
  if (!result.error.empty())
    blog(LOG_ERROR, "[multi-replay] Save failed: %s", result.error.c_str());
  else
    blog(LOG_INFO, "[multi-replay] Saved %s (requested %.3fs, actual %.3fs, end delta %.3fms, %s)",
         result.path.u8string().c_str(), result.requested / 1e6, result.actual / 1e6,
         (result.mediaEnd - result.endpoint) / 1e3, result.partial.c_str());
  results_.push_back(result);
  completed_.push_back(std::move(result));
  if (results_.size() > 20) results_.erase(results_.begin());
}
void Capture::worker()
{
  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    condition_.wait_for(lock, std::chrono::milliseconds(100), [&] { return terminate_ || !ready_.empty(); });
    if (terminate_ && ready_.empty()) break;
    settle();
    if (ready_.empty()) continue;
    Job job = std::move(ready_.front());
    ready_.pop_front();
    auto format = format_;
    auto ledger = ledger_;
    writing_ = true;
    active_ = job.stem;
    lock.unlock();
    MuxResult result;
    if (abortWrites_)
      result.error = "Orderly shutdown drain deadline expired";
    else
      result = writeMkv(
          *format, job.clip, job.folder, job.stem,
          [&] {
            if (abortWrites_) throw std::runtime_error("Orderly shutdown drain deadline expired");
          },
          job.allowSpaces);
    Result saved{result.path,
                 result.error,
                 job.clip.partial,
                 job.clip.requestedDuration,
                 job.clip.end - job.clip.start,
                 job.clip.requestedEnd,
                 job.clip.start,
                 job.clip.end};
    saved.id = job.id;
    if (!saved.error.empty()) saved.error += "\nDestination: " + job.folder.u8string();
    std::vector<PacketPtr>().swap(job.clip.packets);
    ledger->used -= job.metadata;
    lock.lock();
    record(std::move(saved));
    writing_ = false;
    active_.clear();
    finishIfDrained();
    condition_.notify_all();
  }
}
Status Capture::status() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  Status s;
  s.collecting = collecting_;
  s.draining = !collecting_ && (captureActive_ || writing_ || !pending_.empty() || !ready_.empty());
  s.needsStop = needsStop_;
  s.shared = options_.shared && (collecting_ || captureActive_);
  s.paused = collecting_ && paused_ && !resumeNoted_;
  s.degraded = history_ && history_->degraded() && history_->available() < options_.longest;
  s.available = history_ ? history_->available() : 0;
  s.memory = ledger_ ? ledger_->used.load() : 0;
  s.budget = options_.budget;
  s.queued = pending_.size() + ready_.size() + size_t(writing_);
  s.message = message_;
  s.active = active_;
  s.results = results_;
  return s;
}
std::vector<Result> Capture::takeCompleted()
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Result> completed;
  completed.swap(completed_);
  return completed;
}
void Capture::poll()
{
  bool stopNow;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const Time monotonic = clockNow();
    const Time wall =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    if (collecting_ && lastPoll_ && std::abs((wall - lastWall_) - (monotonic - lastPoll_)) > 5000000)
      failCapture("Suspend/resume or system-clock discontinuity; manual restart required");
    lastPoll_ = monotonic;
    lastWall_ = wall;
    if (collecting_ && !(paused_ && !resumeNoted_) && clockNow() - lastPacket_ > 5000000)
      failCapture("Replay encoder stopped delivering packets; manual restart required");
    stopNow = needsStop_ || (captureActive_ && stopRequested_ && (pending_.empty() || monotonic >= stopDeadline_));
  }
  if (stopNow) stop();
}
void Capture::notePause(bool paused)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!options_.shared || !captureActive_) return;
  const Time now = clockNow();
  if (paused && (!paused_ || resumeNoted_)) {
    paused_ = true;
    resumeNoted_ = false;
    pauseEnd_ = now - shift_[0];
  } else if (!paused && paused_ && !resumeNoted_) {
    resumeNoted_ = true;
    resumeWall_ = lastPacket_ = now;
  }
}
void Capture::releaseIdleOutput()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (collecting_ || captureActive_) return;
  }
  if (output_ && !obs_output_active(output_)) {
    obs_output_release(output_);
    output_ = nullptr;
  }
}
void Capture::stopAndWait()
{
  stop();
  // Accepted saves get the normal settlement bound before capture is forced down.
  const Time settleDeadline = clockNow() + 2100000;
  bool active = true;
  while (active) {
    poll();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      active = captureActive_;
    }
    if (!active || clockNow() >= settleDeadline) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (active && output_) obs_output_force_stop(output_);
  // libobs detaches the encoders on its own thread; callers such as a profile
  // switch need them released before OBS resets video.
  const Time released = clockNow() + 3000000;
  while (output_ && obs_output_active(output_) && clockNow() < released)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}
void Capture::shutdown()
{
  if (!worker_.joinable()) return;
  stopAndWait();
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!condition_.wait_for(lock, std::chrono::seconds(15),
                             [&] { return !writing_ && pending_.empty() && ready_.empty(); }))
      abortWrites_ = true;
    terminate_ = true;
    condition_.notify_all();
  }
  worker_.join();
  if (output_) {
    obs_output_release(output_);
    output_ = nullptr;
  }
}
}
