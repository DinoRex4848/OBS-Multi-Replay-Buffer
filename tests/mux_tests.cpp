#include "mux.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}
using namespace replay;
#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr)) throw std::runtime_error(#expr);                                                                      \
  } while (0)
uint64_t checksum(const std::filesystem::path &path)
{
  std::ifstream file(path, std::ios::binary);
  uint64_t value = 1469598103934665603ULL;
  char byte;
  while (file.get(byte)) {
    value ^= uint8_t(byte);
    value *= 1099511628211ULL;
  }
  return value;
}
void fault(const char *value)
{
#ifdef _WIN32
  _putenv_s("REPLAY_TEST_FAULT", value);
#else
  setenv("REPLAY_TEST_FAULT", value, 1);
#endif
}
int main(int argc, char **argv)
{
  if (argc != 5) {
    std::cerr << "Usage: mux_tests WRITER FAULT_WRITER INPUT_MKV OUTPUT_DIR\n";
    return 2;
  }
  try {
    AVFormatContext *input = nullptr;
    CHECK(avformat_open_input(&input, argv[3], nullptr, nullptr) >= 0);
    CHECK(avformat_find_stream_info(input, nullptr) >= 0);
    Format format;
    for (unsigned i = 0; i < input->nb_streams; ++i) {
      auto *stream = input->streams[i];
      auto *par = stream->codecpar;
      Stream s;
      s.codec = avcodec_get_name(par->codec_id);
      s.width = par->width;
      s.height = par->height;
      s.fpsNum = stream->avg_frame_rate.num;
      s.fpsDen = stream->avg_frame_rate.den;
      s.sampleRate = par->sample_rate;
      s.channels = par->ch_layout.nb_channels;
      s.frameSize = par->frame_size;
      s.primaries = par->color_primaries;
      s.transfer = par->color_trc;
      s.colorspace = par->color_space;
      s.range = par->color_range;
      if (auto *title = av_dict_get(stream->metadata, "title", nullptr, 0)) s.name = title->value;
      if (par->extradata_size) s.extra.assign(par->extradata, par->extradata + par->extradata_size);
      format.streams.push_back(std::move(s));
    }
    Selection original;
    original.requestedDuration = 2000000;
    auto *packet = av_packet_alloc();
    CHECK(packet);
    while (av_read_frame(input, packet) >= 0) {
      auto *copy = av_packet_clone(packet);
      CHECK(copy);
      auto owner = std::shared_ptr<void>(copy, [](void *p) {
        auto *pkt = static_cast<AVPacket *>(p);
        av_packet_free(&pkt);
      });
      Packet p;
      p.owner = owner;
      p.data = copy->data;
      p.size = copy->size;
      p.track = copy->stream_index;
      p.key = bool(copy->flags & AV_PKT_FLAG_KEY);
      auto base = input->streams[p.track]->time_base;
      p.pts = av_rescale_q(copy->pts, base, {1, 1000000}) + 1000000;
      p.dts = av_rescale_q(copy->dts, base, {1, 1000000}) + 1000000;
      p.duration = av_rescale_q(copy->duration, base, {1, 1000000});
      if (!p.track && p.key && !original.start) original.start = p.pts;
      original.end = std::max(original.end, p.pts + p.duration);
      original.packets.push_back(std::make_shared<Packet>(std::move(p)));
      av_packet_unref(packet);
    }
    av_packet_free(&packet);
    avformat_close_input(&input);
    original.requestedEnd = original.end;
    auto output = std::filesystem::absolute(argv[4]);
    std::filesystem::create_directories(output);
    setWriterPath(std::filesystem::absolute(argv[1]));
    auto clip = original;
    // A leading slash is drive-root-relative on Windows, not is_absolute().
    auto rooted = writeMkv(format, clip, output, "/replay-outside-destination");
    CHECK(rooted.error.find("must be relative") != std::string::npos);
    clip = original;
    auto result = writeMkv(format, clip, output, "good");
    CHECK(result.error.empty());
    auto hash = checksum(result.path);
    clip = original;
    auto collision = writeMkv(format, clip, output, "good");
    CHECK(collision.error.empty());
    CHECK(collision.path != result.path);
    CHECK(collision.path.filename() == "good (2).mkv");
    CHECK(hash == checksum(result.path));
    clip = original;
    auto noSpaces = writeMkv(format, clip, output, "good", {}, false);
    CHECK(noSpaces.error.empty());
    CHECK(noSpaces.path.filename() == "good_2.mkv");
    CHECK(hash == checksum(result.path));
    clip = original;
    auto nested = writeMkv(format, clip, output, "2026/10/12/Replay 60s partial");
    CHECK(nested.error.empty());
    CHECK(std::filesystem::exists(output / "2026/10/12/Replay 60s partial.mkv"));
    // Both final clips and abandoned incomplete files reserve their names.
    std::ofstream(output / "blocked.incomplete.mkv") << "unfinished";
    clip = original;
    auto incompleteCollision = writeMkv(format, clip, output, "blocked");
    CHECK(incompleteCollision.error.empty());
    CHECK(incompleteCollision.path.filename() == "blocked (2).mkv");
    CHECK(checksum(output / "blocked.incomplete.mkv") != 0);
    clip = original;
    CHECK(!writeMkv(format, clip, output / "missing-directory", "missing").error.empty());
    setWriterPath(output / "missing-helper");
    clip = original;
    CHECK(!writeMkv(format, clip, output, "missing-helper").error.empty());
    setWriterPath(std::filesystem::absolute(argv[2]));
    fault("fail");
    clip = original;
    auto failed = writeMkv(format, clip, output, "disk-full");
    CHECK(failed.error.find("disk full") != std::string::npos);
    fault("stall");
    clip = original;
    auto begin = std::chrono::steady_clock::now();
    auto stalled = writeMkv(format, clip, output, "stalled");
    CHECK(!stalled.error.empty());
    CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds(13));
    CHECK(!std::filesystem::exists(output / "stalled.mkv"));
    CHECK(!std::filesystem::exists(output / "disk-full.mkv"));
    std::cout << "PASS: direct encoded remux, atomic collision protection, missing folder/helper, visible disk-full "
                 "failure, bounded stalled writer\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
