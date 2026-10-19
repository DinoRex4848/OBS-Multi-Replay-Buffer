#include "history.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace replay;
#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr)) throw std::runtime_error(#expr);                                                                      \
  } while (0)
PacketPtr add(History &h, Time pts, bool key, size_t bytes = 100, int track = 0, Time dts = -1)
{
  CHECK(h.prepare(bytes));
  Packet p;
  p.pts = pts;
  p.dts = dts < 0 ? pts : dts;
  p.duration = 1000000;
  p.key = key;
  p.size = bytes;
  p.track = track;
  auto result = h.retain(std::move(p));
  h.push(result);
  return result;
}
int main()
{
  try {
    auto ledger = std::make_shared<Ledger>();
    ledger->limit = 100000000;
    History history(ledger, 600000000);
    for (int t = 1; t <= 1000; ++t) add(history, Time(t) * 1000000, t % 7 == 1);
    for (auto duration : {60000000LL, 240000000LL, 600000000LL}) {
      auto clip = history.select(1000000000, duration);
      finalizeSelection(clip);
      CHECK(clip.start <= 1000000000 - duration);
      CHECK(1000000000 - duration - clip.start < 7000000);
      CHECK(clip.partial.empty());
      CHECK(clip.packets.front()->key);
      CHECK(clip.end - clip.start >= duration);
    }
    auto clip = history.select(1000000000, 60000000);
    auto before = ledger->used.load();
    auto second = history.select(1000000000, 60000000);
    CHECK(before == ledger->used.load());
    CHECK(clip.packets.front() == second.packets.front());
    history.clear();
    CHECK(ledger->used.load() > 0);
    clip.packets.clear();
    second.packets.clear();
    CHECK(ledger->used.load() == 0);
    History warmup(ledger, 600000000);
    add(warmup, 1000000, false);
    bool rejected = false;
    try {
      warmup.select(2000000, 60000000);
    } catch (...) {
      rejected = true;
    }
    CHECK(rejected);
    add(warmup, 2000000, true);
    add(warmup, 3000000, false);
    auto partial = warmup.select(3000000, 60000000);
    finalizeSelection(partial);
    CHECK(!partial.partial.empty());
    warmup.clear();
    partial.packets.clear();
    History reordered(ledger, 10000000);
    add(reordered, 2000000, true, 100, 0, 1000000);
    auto future = add(reordered, 5000000, false, 100, 0, 2000000);
    add(reordered, 3000000, false, 100, 0, 3000000);
    add(reordered, 6000000, false, 100, 0, 4000000);
    auto reorderedClip = reordered.select(3500000, 1500000);
    finalizeSelection(reorderedClip);
    CHECK(reorderedClip.packets.size() == 3);
    CHECK(reorderedClip.packets[1] == future);
    CHECK(reorderedClip.requestedEnd == 3500000);
    History audioOffset(ledger, 10000000);
    auto leadingAudio = add(audioOffset, 900000, false, 100, 1);
    add(audioOffset, 1000000, true);
    add(audioOffset, 2000000, false);
    auto withAudio = audioOffset.select(2000000, 1000000);
    finalizeSelection(withAudio);
    CHECK(withAudio.packets.front() == leadingAudio);
    CHECK(withAudio.start == 1000000);
    History lateDisplay(ledger, 10000000);
    add(lateDisplay, 1000000, true);
    add(lateDisplay, 1800000, false, 100, 0, 1200000);
    add(lateDisplay, 1400000, false, 100, 0, 1800000);
    auto lateClip = lateDisplay.select(1500000, 500000);
    finalizeSelection(lateClip);
    CHECK(lateClip.packets.size() == 3);
    CHECK(lateClip.packets.back()->pts == 1400000);
    auto small = std::make_shared<Ledger>();
    small->limit = 3 * (100 + packetOverhead);
    History pressure(small, 100000000);
    add(pressure, 1000000, true);
    add(pressure, 2000000, false);
    add(pressure, 3000000, true);
    auto pinned = pressure.select(3000000, 3000000);
    CHECK(!pressure.prepare(100));
    CHECK(pressure.degraded());
    CHECK(pressure.count() == 3); // Pinned GOPs stay in history; dropping them would free nothing.
    pinned.packets.clear();
    CHECK(pressure.prepare(100));
    CHECK(pressure.select(3000000, 60000000).partial == "memory pressure");
    // A save accepted under pressure reserves room first, so collection survives its settle period.
    auto tight = std::make_shared<Ledger>();
    tight->limit = 40 * (100 + packetOverhead);
    History saturated(tight, 100000000);
    for (int t = 1; t <= 200; ++t) add(saturated, Time(t) * 100000, t % 10 == 1);
    CHECK(saturated.degraded());
    saturated.reserve(saturated.recentBytes(1500000));
    auto whole = saturated.select(20000000, 100000000);
    const size_t kept = saturated.count();
    for (int t = 201; t <= 210; ++t) add(saturated, Time(t) * 100000, t % 10 == 1);
    CHECK(saturated.count() == kept + 10);
    CHECK(saturated.packets().front() == whole.packets.front());
    whole.packets.clear();
    for (int t = 211; t <= 300; ++t) add(saturated, Time(t) * 100000, t % 10 == 1);
    // Incremental eviction and duration must match a full rescan of the history.
    auto roomy = std::make_shared<Ledger>();
    roomy->limit = 1000000000;
    History indexed(roomy, 20000000);
    std::deque<PacketPtr> mirror;
    Time videoTime = 1000000, audioTime = 1000000;
    unsigned seed = 7;
    for (int i = 0; i < 20000; ++i) {
      seed = seed * 1103515245u + 12345u;
      PacketPtr p;
      if (audioTime < videoTime) {
        p = add(indexed, audioTime, false, 50, 1);
        audioTime += 21333;
      } else {
        p = add(indexed, videoTime + 33334, i == 0 || (seed >> 16) % 90 == 0, 50, 0, videoTime);
        videoTime += 16667;
      }
      mirror.push_back(p);
      Time floor = 0;
      for (auto &q : mirror)
        if (!q->track && q->key && q->pts <= p->pts - 20000000) floor = q->dts;
      if (floor)
        while (!mirror.empty() && mirror.front()->dts < floor) mirror.pop_front();
      CHECK(mirror.size() == indexed.count());
      CHECK(mirror.front() == indexed.packets().front());
      if (i % 500) continue;
      PacketPtr first;
      Time last = 0;
      for (auto &q : mirror) {
        if (q->track) continue;
        if (!first && q->key) first = q;
        last = std::max(last, q->pts + q->duration);
      }
      CHECK(indexed.available() == (first ? std::max(Time(0), last - first->pts) : 0));
    }
    std::cout << "PASS: 60/240/600s earlier keyframes, warmup, shared pins, B-frame dependencies, frozen endpoints, "
                 "straddling audio, pressure admission, pinned history, save reserve, indexed eviction\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
