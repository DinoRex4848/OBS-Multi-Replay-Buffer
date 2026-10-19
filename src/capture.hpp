#pragma once
#include "history.hpp"
#include "mux.hpp"
#include "naming.hpp"
#include <obs-module.h>
#include <array>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace replay {
Time clockNow();
void registerReplayOutput();
struct Options {
  uint64_t budget = 6000000000ULL;
  Time longest = 600000000;
  std::filesystem::path folder;
  FilenameOptions filenames;
  // The encoders belong to the OBS frontend's recording output. Pausing that
  // recording pauses them, so paused time is cut out of the replay timeline.
  bool shared = false;
  std::vector<std::string> trackNames; // Audio track titles in output order; empty uses encoder names.
};
struct Result {
  std::filesystem::path path;
  std::string error, partial;
  Time requested = 0, actual = 0, endpoint = 0;
  Time mediaStart = 0, mediaEnd = 0;
  uint64_t id = 0; // Completion identity, never included in the filename.
};
struct Status {
  bool collecting = false, draining = false, degraded = false, needsStop = false, shared = false, paused = false;
  Time available = 0;
  uint64_t memory = 0, budget = 0;
  size_t queued = 0;
  std::string message, active;
  std::vector<Result> results;
};
class Capture {
public:
  Capture();
  ~Capture();
  Capture(const Capture &) = delete;
  Capture &operator=(const Capture &) = delete;
  // Takes references to configured encoders. Caller may release its references
  // after this call. They are plugin-owned unless Options::shared is set.
  void start(const Options &, obs_encoder_t *video, const std::vector<obs_encoder_t *> &audio);
  void stop();
  // Stops and returns once libobs has detached the encoders (bounded wait).
  void stopAndWait();
  // Drops the previous session's output and its encoder references.
  void releaseIdleOutput();
  // Shared mode only: the frontend paused or resumed the recording that owns the encoders.
  void notePause(bool paused);
  void shutdown();
  // Returns the completion request ID. A nonempty destination overrides this request's folder.
  uint64_t save(Time duration, Time endpoint, const std::filesystem::path &destination = {});
  Status status() const;
  // Drain notifications independently of the bounded recent-results snapshot.
  std::vector<Result> takeCompleted();
  void poll();
  // Output callbacks, public only for libobs's C ABI.
  bool begin(obs_output_t *);
  void end();
  void receive(encoder_packet *);

private:
  struct Job {
    Selection clip;
    std::filesystem::path folder;
    std::string stem;
    uint64_t metadata = 0;
    size_t capacity = 0;
    Time accepted = 0;
    bool frozen = false; // Accepted while paused: nothing more can arrive for it.
    uint64_t id = 0;
    bool allowSpaces = true;
  };
  void worker();
  void settle();
  void finishIfDrained();
  void failCapture(std::string);
  void record(Result);
  std::shared_ptr<Format> describeOutput(obs_output_t *) const;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::thread worker_;
  bool terminate_ = false, collecting_ = false, captureActive_ = false, needsStop_ = false, writing_ = false;
  bool headersReady_ = false;
  bool stopRequested_ = false, paused_ = false, resumeNoted_ = false;
  Time stopDeadline_ = 0;
  std::atomic<bool> abortWrites_{false};
  std::unique_ptr<History> history_;
  std::shared_ptr<Ledger> ledger_;
  std::shared_ptr<const Format> format_;
  Options options_;
  FilenameVideo namingVideo_;
  std::deque<Job> pending_, ready_;
  std::vector<Result> results_;
  std::vector<Result> completed_;
  // watermark_ is on the replay timeline; rawMark_ is system time, which jumps
  // by shift_ whenever shared encoders were paused.
  std::array<Time, 7> watermark_{}, rawMark_{}, shift_{}, step_{};
  std::array<bool, 7> primed_{};
  Time pauseEnd_ = 0, resumeWall_ = 0;
  Time lastPacket_ = 0;
  Time lastPoll_ = 0, lastWall_ = 0;
  uint64_t sequence_ = 0;
  std::string session_, message_, active_;
  obs_output_t *output_ = nullptr;
};
}
