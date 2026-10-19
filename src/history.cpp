#include "history.hpp"
#include <algorithm>
#include <stdexcept>

namespace replay {
History::History(std::shared_ptr<Ledger> ledger, Time target) : ledger_(std::move(ledger)), target_(target) {}
bool History::dropGop()
{
  // packets_ and keyframes_ hold one reference each; anything more is an accepted
  // save. A GOP the writer has not passed yet frees nothing when dropped, so
  // stop there instead of discarding history that cannot be reclaimed.
  if (keyframes_.size() < 2 || keyframes_[0].use_count() > 2 || keyframes_[1].use_count() > 2) return false;
  const Time floor = keyframes_[1]->dts;
  keyframes_.pop_front();
  while (!packets_.empty() && packets_.front()->dts < floor) packets_.pop_front();
  return true;
}
bool History::prepare(size_t payload)
{
  const uint64_t need = uint64_t(payload) + packetOverhead;
  if (need > ledger_->limit) return false;
  while (ledger_->used.load() > ledger_->limit - need) {
    pressure_ = true;
    if (!dropGop()) return false;
  }
  return true;
}
void History::reserve(uint64_t bytes)
{
  while (bytes < ledger_->limit && ledger_->used.load() > ledger_->limit - bytes) {
    if (!dropGop()) return;
    pressure_ = true;
  }
}
uint64_t History::recentBytes(Time window) const
{
  uint64_t bytes = 0;
  for (auto it = packets_.rbegin(); it != packets_.rend() && (*it)->dts >= packets_.back()->dts - window; ++it)
    bytes += (*it)->size + packetOverhead;
  return bytes;
}
PacketPtr History::retain(Packet packet)
{
  const uint64_t bytes = packet.size + packetOverhead;
  auto ledger = ledger_;
  auto *raw = new Packet(std::move(packet));
  ledger->used += bytes;
  return PacketPtr(raw, [ledger, bytes](const Packet *p) {
    delete p;
    ledger->used -= bytes;
  });
}
void History::push(PacketPtr packet)
{
  if (!packet->track) {
    if (packet->key) keyframes_.push_back(packet);
    lastVideoEnd_ = std::max(lastVideoEnd_, packet->pts + packet->duration);
  }
  packets_.push_back(std::move(packet));
  evict(packets_.back()->pts);
}
void History::evict(Time now)
{
  // Keep the latest keyframe at/before target start, without assuming GOP length.
  while (keyframes_.size() > 1 && keyframes_[1]->pts <= now - target_) keyframes_.pop_front();
  if (keyframes_.empty() || keyframes_.front()->pts > now - target_) return;
  const Time floor = keyframes_.front()->dts;
  while (!packets_.empty() && packets_.front()->dts < floor) packets_.pop_front();
}
Selection History::select(Time end, Time duration) const
{
  Selection s;
  s.packets.reserve(packets_.size());
  s.requestedEnd = end;
  s.requestedDuration = duration;
  PacketPtr first, chosen;
  for (auto &p : packets_) {
    if (p->track || !p->key || p->pts > end) continue;
    if (!first) first = p;
    if (p->pts <= end - duration) chosen = p;
  }
  if (!chosen) {
    chosen = first;
    s.partial = pressure_ ? "memory pressure" : "startup/session reset";
  }
  if (!chosen) throw std::runtime_error("No playable replay available yet");
  s.start = chosen->pts;
  s.decodeStart = chosen->dts;
  for (auto &p : packets_) {
    bool afterStart = p->track ? p->pts + p->duration > chosen->pts : p->dts >= chosen->dts;
    if (afterStart && p->dts <= end + (p->track ? 0 : settleWindow)) s.packets.push_back(p);
  }
  if (s.packets.empty()) throw std::runtime_error("No playable replay available yet");
  return s;
}
void finalizeSelection(Selection &s)
{
  Time lastDecode = -1;
  s.end = s.start;
  for (auto &p : s.packets) {
    if (p->track == 0 && p->pts <= s.requestedEnd) lastDecode = std::max(lastDecode, p->dts);
  }
  s.packets.erase(std::remove_if(s.packets.begin(), s.packets.end(),
                                 [&](auto &p) {
                                   return p->track == 0 ? p->dts > lastDecode
                                                        : (p->pts + p->duration <= s.start || p->pts > s.requestedEnd);
                                 }),
                  s.packets.end());
  std::stable_sort(s.packets.begin(), s.packets.end(), [](auto &a, auto &b) { return a->dts < b->dts; });
  for (auto &p : s.packets)
    if (!p->track) s.end = std::max(s.end, p->pts + p->duration);
  if (s.end - s.start + 1000 < s.requestedDuration && s.partial.empty()) s.partial = "insufficient settled history";
}
void History::clear()
{
  packets_.clear();
  keyframes_.clear();
  lastVideoEnd_ = 0;
}
Time History::available() const
{
  return keyframes_.empty() ? 0 : std::max(Time(0), lastVideoEnd_ - keyframes_.front()->pts);
}
}
