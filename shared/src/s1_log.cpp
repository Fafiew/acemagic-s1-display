// s1_log.cpp - implementation of shared structured logging.
// License: GPL-3.0 (see LICENSE).

#include "s1_log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>

namespace s1 {
namespace {

std::atomic<ILogSink*> g_sink{nullptr};
std::atomic<int> g_level{static_cast<int>(LogLevel::kInfo)};

// A short UTC timestamp "YYYY-MM-DDThh:mm:ss.mmmZ".
std::string Timestamp() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto secs = time_point_cast<seconds>(now);
  const auto ms = duration_cast<milliseconds>(now - secs).count();
  std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec, static_cast<int>(ms));
  return buf;
}

// Strip a directory prefix from a __FILE__ path.
const char* BaseName(const char* path) {
  const char* base = path;
  for (const char* p = path; *p; ++p) {
    if (*p == '/' || *p == '\\') base = p + 1;
  }
  return base;
}

}  // namespace

const char* Log::LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kTrace: return "trace";
    case LogLevel::kDebug: return "debug";
    case LogLevel::kInfo:  return "info";
    case LogLevel::kWarn:  return "warn";
    case LogLevel::kError: return "error";
  }
  return "unknown";
}

void Log::SetSink(ILogSink* sink) { g_sink.store(sink); }
void Log::SetLevel(LogLevel level) { g_level.store(static_cast<int>(level)); }
LogLevel Log::Level() { return static_cast<LogLevel>(g_level.load()); }

std::string Log::Format(LogLevel level, const char* file, int line,
                        const char* fmt, va_list args) {
  char msg[2048];
  va_list copy;
  va_copy(copy, args);
  std::vsnprintf(msg, sizeof(msg), fmt, copy);
  va_end(copy);

  std::string out;
  out.reserve(256);
  out += Timestamp();
  out += ' ';
  out += LevelName(level);
  out += " [";
  out += BaseName(file);
  out += ':';
  out += std::to_string(line);
  out += "] ";
  out += msg;
  return out;
}

void Log::Write(LogLevel level, const char* file, int line, const char* fmt, ...) {
  if (static_cast<int>(level) < g_level.load()) return;
  ILogSink* sink = g_sink.load();
  if (!sink) return;

  va_list args;
  va_start(args, fmt);
  const std::string line_text = Format(level, file, line, fmt, args);
  va_end(args);

  sink->Write(level, line_text.c_str());
}

// ---------------------------------------------------------------------------
// FileLogSink
// ---------------------------------------------------------------------------

namespace {
struct FileLogState {
  std::FILE* file = nullptr;
  std::mutex mutex;
};
}  // namespace

FileLogSink::FileLogSink(const std::string& path, size_t maxBytes)
    : path_(path), maxBytes_(maxBytes) {
  auto* st = new FileLogState();
  st->file = std::fopen(path_.c_str(), "ab");
  impl_ = st;
  if (st->file) {
    std::fseek(st->file, 0, SEEK_END);
    long pos = std::ftell(st->file);
    written_ = pos > 0 ? static_cast<size_t>(pos) : 0;
  }
}

FileLogSink::~FileLogSink() {
  auto* st = static_cast<FileLogState*>(impl_);
  if (st) {
    std::lock_guard<std::mutex> lock(st->mutex);
    if (st->file) std::fclose(st->file);
    delete st;
  }
}

void FileLogSink::RotateIfNeeded() {
  if (written_ < maxBytes_) return;
  auto* st = static_cast<FileLogState*>(impl_);
  if (st->file) std::fclose(st->file);
  const std::string oldPath = path_ + ".1";
  std::remove(oldPath.c_str());
  std::rename(path_.c_str(), oldPath.c_str());
  st->file = std::fopen(path_.c_str(), "ab");
  written_ = 0;
}

void FileLogSink::Write(LogLevel /*level*/, const char* line) {
  auto* st = static_cast<FileLogState*>(impl_);
  if (!st || !st->file) return;
  std::lock_guard<std::mutex> lock(st->mutex);
  RotateIfNeeded();
  const int n = std::fprintf(st->file, "%s\n", line);
  if (n > 0) written_ += static_cast<size_t>(n);
  std::fflush(st->file);
}

}  // namespace s1
