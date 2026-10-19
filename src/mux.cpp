#include "mux.hpp"
#include "wire.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <linux/fs.h>
#include <unistd.h>
extern char **environ;
#endif

namespace replay {
static std::filesystem::path helper;
void setWriterPath(std::filesystem::path p) { helper = std::move(p); }
namespace {
class Child {
public:
#ifdef _WIN32
  HANDLE input = nullptr, process = nullptr;
  HANDLE diagnostics = nullptr;
  std::mutex watchMutex;
  std::condition_variable watchCondition;
  std::thread watchdog;
  bool watchDone = false;
  std::chrono::steady_clock::time_point activity = std::chrono::steady_clock::now();
#else
  int input = -1;
  int diagnostics = -1;
  pid_t process = -1;
#endif
  explicit Child(const std::filesystem::path &exe)
  {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe;
    if (!CreatePipe(&readPipe, &input, &sa, 0)) throw std::runtime_error("Cannot create writer pipe");
    SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
    HANDLE errorPipe;
    if (!CreatePipe(&diagnostics, &errorPipe, &sa, 0)) {
      CloseHandle(readPipe);
      CloseHandle(input);
      input = nullptr;
      throw std::runtime_error("Cannot create writer diagnostic pipe");
    }
    SetHandleInformation(diagnostics, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = readPipe;
    si.hStdOutput = errorPipe;
    si.hStdError = errorPipe;
    PROCESS_INFORMATION pi{};
    auto command = L"\"" + exe.wstring() + L"\"";
    BOOL ok = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                             &si, &pi);
    CloseHandle(readPipe);
    CloseHandle(errorPipe);
    if (!ok) {
      CloseHandle(input);
      CloseHandle(diagnostics);
      input = diagnostics = nullptr;
      throw std::runtime_error("Cannot launch MKV writer helper");
    }
    process = pi.hProcess;
    CloseHandle(pi.hThread);
    watchdog = std::thread([this] {
      std::unique_lock<std::mutex> lock(watchMutex);
      while (!watchDone) {
        auto deadline = activity + std::chrono::seconds(10);
        if (watchCondition.wait_until(lock, deadline) == std::cv_status::timeout && !watchDone &&
            std::chrono::steady_clock::now() >= activity + std::chrono::seconds(10)) {
          TerminateProcess(process, 2);
          break;
        }
      }
    });
#else
    int pipes[2];
    if (pipe(pipes)) throw std::runtime_error("Cannot create writer pipe");
    int errors[2];
    if (pipe(errors)) {
      close(pipes[0]);
      close(pipes[1]);
      throw std::runtime_error("Cannot create writer diagnostic pipe");
    }
    fcntl(pipes[1], F_SETFD, FD_CLOEXEC);
    fcntl(errors[0], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipes[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipes[0]);
    posix_spawn_file_actions_addclose(&actions, pipes[1]);
    posix_spawn_file_actions_adddup2(&actions, errors[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, errors[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, errors[0]);
    posix_spawn_file_actions_addclose(&actions, errors[1]);
    std::string path = exe.string();
    char *args[] = {path.data(), nullptr};
    int error = posix_spawn(&process, path.c_str(), &actions, nullptr, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipes[0]);
    close(errors[1]);
    if (error) {
      close(pipes[1]);
      close(errors[0]);
      process = -1;
      throw std::runtime_error("Cannot launch MKV writer helper: " + std::string(strerror(error)));
    }
    input = pipes[1];
    fcntl(input, F_SETFL, O_NONBLOCK);
    diagnostics = errors[0];
    fcntl(diagnostics, F_SETFL, O_NONBLOCK);
#endif
  }
  ~Child()
  {
#ifdef _WIN32
    {
      std::lock_guard<std::mutex> lock(watchMutex);
      watchDone = true;
      watchCondition.notify_all();
    }
    if (watchdog.joinable()) watchdog.join();
    closeInput();
    if (process) {
      TerminateProcess(process, 1);
      WaitForSingleObject(process, 1000);
      CloseHandle(process);
    }
    if (diagnostics) CloseHandle(diagnostics);
#else
    closeInput();
    if (process > 0) {
      kill(process, SIGKILL);
      waitpid(process, nullptr, 0);
    }
    if (diagnostics >= 0) close(diagnostics);
#endif
  }
  std::string diagnostic()
  {
    std::string message;
    char buffer[4096];
#ifdef _WIN32
    DWORD available = 0, read = 0;
    if (diagnostics && PeekNamedPipe(diagnostics, nullptr, 0, nullptr, &available, nullptr) && available &&
        ReadFile(diagnostics, buffer, std::min(DWORD(sizeof(buffer)), available), &read, nullptr))
      message.assign(buffer, read);
#else
    auto size = read(diagnostics, buffer, sizeof(buffer));
    if (size > 0) message.assign(buffer, size_t(size));
#endif
    return message.empty() ? "MKV writer stopped or disk stalled (10-second limit)" : message;
  }
  void closeInput()
  {
#ifdef _WIN32
    if (input) {
      CloseHandle(input);
      input = nullptr;
    }
#else
    if (input >= 0) {
      close(input);
      input = -1;
    }
#endif
  }
  void send(const void *data, size_t size)
  {
    auto *bytes = static_cast<const uint8_t *>(data);
    while (size) {
#ifdef _WIN32
      DWORD n = 0;
      BOOL ok = WriteFile(input, bytes, DWORD(std::min(size, size_t(65536))), &n, nullptr);
      if (!ok || !n) throw std::runtime_error(diagnostic());
      {
        std::lock_guard<std::mutex> lock(watchMutex);
        activity = std::chrono::steady_clock::now();
        watchCondition.notify_all();
      }
#else
      // SIGPIPE is blocked only on this writer thread; OBS signal policy is untouched.
      pollfd fd{input, POLLOUT, 0};
      int ready;
      do {
        ready = poll(&fd, 1, 10000);
      } while (ready < 0 && errno == EINTR);
      if (ready <= 0 || (fd.revents & (POLLERR | POLLHUP))) throw std::runtime_error(diagnostic());
      auto n = write(input, bytes, std::min(size, size_t(65536)));
      if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
      if (n <= 0) throw std::runtime_error("MKV writer pipe failed");
#endif
      bytes += n;
      size -= size_t(n);
    }
  }
  template <typename T> void send(const T &value) { send(&value, sizeof(value)); }
  int finish()
  {
    closeInput();
#ifdef _WIN32
    if (WaitForSingleObject(process, 10000) != WAIT_OBJECT_0)
      throw std::runtime_error("MKV finalization stalled (10-second limit)");
    DWORD code;
    GetExitCodeProcess(process, &code);
    {
      std::lock_guard<std::mutex> lock(watchMutex);
      watchDone = true;
      watchCondition.notify_all();
    }
    if (watchdog.joinable()) watchdog.join();
    CloseHandle(process);
    process = nullptr;
    return int(code);
#else
    for (int i = 0; i < 100; ++i) {
      int status = 0;
      auto result = waitpid(process, &status, WNOHANG);
      if (result == process) {
        process = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    throw std::runtime_error("MKV finalization stalled (10-second limit)");
#endif
  }
};
}
MuxResult writeMkv(const Format &format, Selection &clip, const std::filesystem::path &folder, const std::string &stem,
                   std::function<void()> progress, bool allowSpaces)
{
  MuxResult result;
  try {
#ifndef _WIN32
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);
#endif
    unsigned suffix = 0;
    auto candidate = [&] {
      return stem +
             (suffix ? (allowSpaces ? " (" + std::to_string(suffix + 1) + ")" : "_" + std::to_string(suffix + 1)) : "");
    };
    auto path = folder / std::filesystem::u8path(candidate() + ".mkv");
    auto incomplete = folder / std::filesystem::u8path(candidate() + ".incomplete.mkv");
    if (!std::filesystem::is_directory(folder))
      throw std::runtime_error("Replay destination is missing or not a directory");
    const auto relative = std::filesystem::u8path(stem);
    if (relative.has_root_path()) throw std::runtime_error("Replay filename must be relative");
    for (const auto &part : relative)
      if (part == "..") throw std::runtime_error("Replay filename cannot leave the destination folder");
    std::filesystem::create_directories(path.parent_path());
    while (std::filesystem::exists(path) || std::filesystem::exists(incomplete)) {
      if (++suffix > 1000)
        throw std::runtime_error("Replay filename collision capacity exceeded; existing files were preserved");
      path = folder / std::filesystem::u8path(candidate() + ".mkv");
      incomplete = folder / std::filesystem::u8path(candidate() + ".incomplete.mkv");
    }
    Child child(helper);
    auto filename = incomplete.u8string();
    WireHeader header;
    header.streams = uint32_t(format.streams.size());
    header.pathSize = uint32_t(filename.size());
    header.packets = clip.packets.size();
    child.send(header);
    child.send(filename.data(), filename.size());
    for (size_t index = 0; index < format.streams.size(); ++index) {
      auto &s = format.streams[index];
      WireStream w;
      w.codecSize = uint32_t(s.codec.size());
      w.nameSize = uint32_t(s.name.size());
      w.extraSize = uint32_t(s.extra.size());
      w.width = s.width;
      w.height = s.height;
      w.fpsNum = s.fpsNum;
      w.fpsDen = s.fpsDen;
      w.sampleRate = s.sampleRate;
      w.channels = s.channels;
      w.frameSize = s.frameSize;
      for (auto &packet : clip.packets)
        if (packet->track == int(index)) {
          w.priming = packet->priming;
          break;
        }
      w.primaries = s.primaries;
      w.transfer = s.transfer;
      w.colorspace = s.colorspace;
      w.range = s.range;
      w.maxLuminance = s.maxLuminance;
      child.send(w);
      child.send(s.codec.data(), s.codec.size());
      child.send(s.name.data(), s.name.size());
      child.send(s.extra.data(), s.extra.size());
    }
    // Rebase every stream against the SAME system presentation origin. Keep
    // negative video DTS for B-frame decoding; the muxer shifts streams together.
    for (auto &p : clip.packets) {
      if (p->size > maxPacketSize) throw std::runtime_error("Encoded packet exceeds the writer's 8 MiB bound");
      WirePacket w{p->pts - clip.start, p->dts - clip.start, p->duration,
                   uint32_t(p->track),  p->key ? 1U : 0U,    uint32_t(p->size)};
      child.send(w);
      child.send(p->data, p->size);
      // Accepted jobs release their pins as soon as each payload reaches the
      // bounded helper pipe. Other jobs/history still share their own pins.
      p.reset();
      if (progress) progress();
    }
    int code = child.finish();
    if (code) throw std::runtime_error("MKV writer failed (code " + std::to_string(code) + "): " + child.diagnostic());
    // Atomic publication fails rather than overwriting an existing final name.
#ifdef _WIN32
    while (!MoveFileW(incomplete.c_str(), path.c_str())) {
      auto error = GetLastError();
      if ((error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) || ++suffix > 1000)
        throw std::runtime_error("Cannot publish replay filename; incomplete file retained");
      path = folder / std::filesystem::u8path(candidate() + ".mkv");
    }
#else
    while (syscall(SYS_renameat2, AT_FDCWD, incomplete.c_str(), AT_FDCWD, path.c_str(), RENAME_NOREPLACE)) {
      if (errno != EEXIST || ++suffix > 1000)
        throw std::runtime_error("Cannot publish replay filename; incomplete file retained: " +
                                 std::string(strerror(errno)));
      path = folder / std::filesystem::u8path(candidate() + ".mkv");
    }
#endif
    result.path = path;
  } catch (const std::exception &e) {
    result.error = e.what();
  }
  return result;
}
}
