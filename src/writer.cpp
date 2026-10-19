#include "wire.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mastering_display_metadata.h>
}
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
void readBytes(void *data, size_t size)
{
  if (size && fread(data, 1, size, stdin) != size) throw std::runtime_error("Truncated writer input");
}
template <typename T> T readValue()
{
  T value{};
  readBytes(&value, sizeof(value));
  return value;
}
std::string readString(uint32_t size)
{
  if (size > 1024 * 1024) throw std::runtime_error("Invalid string size");
  std::string value(size, '\0');
  readBytes(value.data(), size);
  return value;
}
void checked(int result, const char *op)
{
  if (result >= 0) return;
  char error[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(result, error, sizeof(error));
  throw std::runtime_error(std::string(op) + ": " + error);
}
int writeFile(void *opaque, const uint8_t *data, int size)
{
  auto *file = static_cast<FILE *>(opaque);
  return fwrite(data, 1, size, file) == size_t(size) ? size : AVERROR(errno ? errno : EIO);
}
int64_t seekFile(void *opaque, int64_t offset, int whence)
{
  auto *file = static_cast<FILE *>(opaque);
  if (whence == AVSEEK_SIZE) {
#ifdef _WIN32
    auto old = _ftelli64(file);
    _fseeki64(file, 0, SEEK_END);
    auto size = _ftelli64(file);
    _fseeki64(file, old, SEEK_SET);
#else
    auto old = ftello(file);
    fseeko(file, 0, SEEK_END);
    auto size = ftello(file);
    fseeko(file, old, SEEK_SET);
#endif
    return size;
  }
#ifdef _WIN32
  if (_fseeki64(file, offset, whence & ~AVSEEK_FORCE)) return AVERROR(EIO);
  return _ftelli64(file);
#else
  if (fseeko(file, offset, whence & ~AVSEEK_FORCE)) return AVERROR(EIO);
  return ftello(file);
#endif
}
}
int main()
{
  av_log_set_level(AV_LOG_ERROR);
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
#endif
  AVFormatContext *ctx = nullptr;
  AVIOContext *io = nullptr;
  FILE *file = nullptr;
  AVPacket *packet = nullptr;
  int result = 1;
  try {
    auto header = readValue<replay::WireHeader>();
    if (header.magic != replay::wireMagic || !header.streams || header.streams > 7)
      throw std::runtime_error("Invalid writer protocol");
    auto path = std::filesystem::u8path(readString(header.pathSize));
#ifdef _WIN32
    int fd = _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd >= 0) file = _fdopen(fd, "wb");
#else
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) file = fdopen(fd, "wb");
#endif
    if (!file)
      throw std::runtime_error("Cannot exclusively create incomplete replay file: " + std::string(strerror(errno)));
    checked(avformat_alloc_output_context2(&ctx, nullptr, "matroska", nullptr), "Allocate Matroska context");
    auto *buffer = static_cast<uint8_t *>(av_malloc(65536));
    if (!buffer) throw std::bad_alloc();
    io = avio_alloc_context(buffer, 65536, 1, file, nullptr, writeFile, seekFile);
    if (!io) {
      av_free(buffer);
      throw std::bad_alloc();
    }
    ctx->pb = io;
    ctx->flags |= AVFMT_FLAG_CUSTOM_IO;
    ctx->avoid_negative_ts = AVFMT_AVOID_NEG_TS_MAKE_NON_NEGATIVE;
    // Bounded interleave queue. Parent packets are already in decode order.
    ctx->max_interleave_delta = 250000;
    for (uint32_t i = 0; i < header.streams; ++i) {
      auto s = readValue<replay::WireStream>();
      auto codec = readString(s.codecSize), name = readString(s.nameSize);
      if (s.extraSize > 1024 * 1024) throw std::runtime_error("Invalid codec header size");
      auto *desc = avcodec_descriptor_get_by_name(codec.c_str());
      if (!desc) throw std::runtime_error("Unknown codec: " + codec);
      auto *stream = avformat_new_stream(ctx, nullptr);
      if (!stream) throw std::bad_alloc();
      auto *par = stream->codecpar;
      par->codec_type = desc->type;
      par->codec_id = desc->id;
      par->extradata = static_cast<uint8_t *>(av_mallocz(size_t(s.extraSize) + AV_INPUT_BUFFER_PADDING_SIZE));
      if (!par->extradata) throw std::bad_alloc();
      par->extradata_size = int(s.extraSize);
      readBytes(par->extradata, s.extraSize);
      if (desc->id == AV_CODEC_ID_OPUS && !s.priming && s.extraSize >= 19) {
        // OpusHead pre-skip refers to encoder startup, not a mid-session clip.
        par->extradata[10] = 0;
        par->extradata[11] = 0;
      }
      stream->time_base = {1, 1000000};
      if (!i) {
        par->width = s.width;
        par->height = s.height;
        par->color_primaries = AVColorPrimaries(s.primaries);
        par->color_trc = AVColorTransferCharacteristic(s.transfer);
        par->color_space = AVColorSpace(s.colorspace);
        par->color_range = AVColorRange(s.range);
        stream->avg_frame_rate = {s.fpsNum, s.fpsDen};
        if (s.maxLuminance) {
          size_t size;
          auto *light = av_content_light_metadata_alloc(&size);
          light->MaxCLL = light->MaxFALL = s.maxLuminance;
          av_packet_side_data_add(&par->coded_side_data, &par->nb_coded_side_data, AV_PKT_DATA_CONTENT_LIGHT_LEVEL,
                                  reinterpret_cast<uint8_t *>(light), size, 0);
          auto *md = av_mastering_display_metadata_alloc();
          md->display_primaries[0][0] = {17, 25};
          md->display_primaries[0][1] = {8, 25};
          md->display_primaries[1][0] = {53, 200};
          md->display_primaries[1][1] = {69, 100};
          md->display_primaries[2][0] = {3, 20};
          md->display_primaries[2][1] = {3, 50};
          md->white_point[0] = {3127, 10000};
          md->white_point[1] = {329, 1000};
          md->max_luminance = {s.maxLuminance, 1};
          md->min_luminance = {0, 1};
          md->has_luminance = md->has_primaries = 1;
          av_packet_side_data_add(&par->coded_side_data, &par->nb_coded_side_data,
                                  AV_PKT_DATA_MASTERING_DISPLAY_METADATA, reinterpret_cast<uint8_t *>(md), sizeof(*md),
                                  0);
        }
      } else {
        par->sample_rate = s.sampleRate;
        par->frame_size = s.frameSize;
        par->initial_padding = s.priming;
        // Priming belongs only to encoder startup, not a mid-session replay.
        av_channel_layout_default(&par->ch_layout, s.channels);
        if (s.channels == 5) {
          av_channel_layout_uninit(&par->ch_layout);
          par->ch_layout = AV_CHANNEL_LAYOUT_4POINT1;
        }
        av_dict_set(&stream->metadata, "title", name.c_str(), 0);
      }
    }
    AVDictionary *options = nullptr;
    av_dict_set(&options, "cluster_size_limit", "1048576", 0);
    av_dict_set(&options, "cluster_time_limit", "1000", 0);
    int headerResult = avformat_write_header(ctx, &options);
    av_dict_free(&options);
    checked(headerResult, "Write MKV header");
    packet = av_packet_alloc();
    if (!packet) throw std::bad_alloc();
    for (uint64_t i = 0; i < header.packets; ++i) {
      auto p = readValue<replay::WirePacket>();
      if (p.track >= header.streams || p.size > replay::maxPacketSize) throw std::runtime_error("Invalid packet");
      checked(av_new_packet(packet, int(p.size)), "Allocate packet");
      readBytes(packet->data, p.size);
      packet->stream_index = int(p.track);
      packet->pts = p.pts;
      packet->dts = p.dts;
      packet->duration = p.duration;
      if (p.key) packet->flags |= AV_PKT_FLAG_KEY;
      av_packet_rescale_ts(packet, {1, 1000000}, ctx->streams[p.track]->time_base);
      const std::string operation = "Write encoded packet " + std::to_string(i) + " track " + std::to_string(p.track);
      // Input has already been sorted by common system DTS. Avoid FFmpeg's
      // additional interleave packet queue so controlled memory stays bounded.
      checked(av_write_frame(ctx, packet), operation.c_str());
      av_packet_unref(packet);
    }
    checked(av_write_trailer(ctx), "Finalize MKV");
    avio_flush(io);
    checked(io->error, "Flush MKV");
    if (fflush(file) || ferror(file)) throw std::runtime_error("Flush replay file failed");
    if (fclose(file)) {
      file = nullptr;
      throw std::runtime_error("Close replay file failed");
    }
    file = nullptr;
    result = 0;
  } catch (const std::exception &e) {
    fprintf(stderr, "[multi-replay writer] %s\n", e.what());
  }
  av_packet_free(&packet);
  if (ctx) ctx->pb = nullptr;
  avformat_free_context(ctx);
  if (io) {
    av_freep(&io->buffer);
    avio_context_free(&io);
  }
  if (file) fclose(file);
  return result;
}
