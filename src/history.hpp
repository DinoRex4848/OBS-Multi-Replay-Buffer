#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace replay {
using Time = int64_t; // Monotonic system timeline, microseconds.
struct Ledger {
  std::atomic<uint64_t> used{0};
  uint64_t limit = 6000000000ULL;
};
// Includes allocation, packet, reference-control-block and index headroom.
constexpr uint64_t packetOverhead = 512;
constexpr uint64_t jobOverhead = 4096;
constexpr uint64_t refOverhead = 32;
constexpr uint64_t writerReserve = 32 * 1024 * 1024;
constexpr Time settleWindow = 500000;
// Room kept free when a save is accepted, for packets that arrive before the writer releases pins.
constexpr Time settleReserve = 3000000;
struct Packet {
  std::shared_ptr<void> owner;
  const uint8_t *data = nullptr;
  size_t size = 0;
  Time dts = 0, pts = 0, duration = 0;
  int track = 0;   // 0 = video, 1..6 = selected audio tracks.
  int priming = 0; // Only the encoder's first audio packet carries startup delay.
  bool key = false;
};
using PacketPtr = std::shared_ptr<const Packet>;
struct Selection {
  std::vector<PacketPtr> packets;
  Time start = 0, decodeStart = 0, end = 0, requestedEnd = 0, requestedDuration = 0;
  std::string partial;
};

// Serialized by Capture's mutex. Jobs and rolling history share packet objects.
class History {
public:
  explicit History(std::shared_ptr<Ledger> ledger, Time target);
  bool prepare(size_t payload); // Must run before allocating a retained packet.
  // Best effort: drops leading GOPs no save pins until `bytes` are free.
  void reserve(uint64_t bytes);
  uint64_t recentBytes(Time window) const;
  PacketPtr retain(Packet packet);
  void push(PacketPtr packet);
  Selection select(Time end, Time duration) const;
  void evict(Time now);
  void clear();
  Time available() const;
  size_t count() const { return packets_.size(); }
  bool degraded() const { return pressure_; }
  const std::deque<PacketPtr> &packets() const { return packets_; }

private:
  bool dropGop();
  std::shared_ptr<Ledger> ledger_;
  std::deque<PacketPtr> packets_;
  std::deque<PacketPtr> keyframes_; // Video keyframes still in packets_, oldest first.
  Time target_, lastVideoEnd_ = 0;
  bool pressure_ = false;
};
// Include a decode-order prefix through the last video frame whose PTS is at
// or before the request. Future-reference frames within this prefix stay intact.
void finalizeSelection(Selection &s);
}
