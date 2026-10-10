// s1_log.h - minimal structured logging shared by all components.
//
// Deliberately tiny and dependency-free. Error paths must never drop
// information silently (see docs/architecture.md, "Engineering constraints"),
// so every component logs at least to a rotating file and, optionally, to the
// Windows debug output.
//
// License: GPL-3.0 (see LICENSE).

#ifndef S1_LOG_H
#define S1_LOG_H

#include <cstdarg>
#include <cstdint>
#include <string>

namespace s1 {

enum class LogLevel : int { kTrace = 0, kDebug = 1, kInfo = 2, kWarn = 3, kError = 4 };

// A sink receives fully formatted log lines. Implementations must be
// thread-safe. Tests install an in-memory sink to assert on emitted lines.
class ILogSink {
 public:
  virtual ~ILogSink() = default;
  virtual void Write(LogLevel level, const char* line) = 0;
};

// Process-wide logger. Set a sink once at startup; if none is set, logs are
// discarded (so the library stays usable in unit tests without side effects).
class Log {
 public:
  static void SetSink(ILogSink* sink);
  static void SetLevel(LogLevel level);
  static LogLevel Level();
  static void Write(LogLevel level, const char* file, int line,
                    const char* fmt, ...);
  // Format into `out` without emitting; used by tests and the control app.
  static std::string Format(LogLevel level, const char* file, int line,
                            const char* fmt, va_list args);

  // Lower-case name for a level ("error", "warn", ...).
  static const char* LevelName(LogLevel level);
};

// Writes to a file, rotating at a fixed size. Thread-safe.
class FileLogSink : public ILogSink {
 public:
  FileLogSink(const std::string& path, size_t maxBytes = 4 * 1024 * 1024);
  ~FileLogSink() override;
  void Write(LogLevel level, const char* line) override;

 private:
  void RotateIfNeeded();

  std::string path_;
  size_t maxBytes_;
  size_t written_ = 0;
  void* impl_ = nullptr;  // opaque handle (FILE* + mutex), avoids <cstdio> in header
};

}  // namespace s1

#define S1_LOG(level, ...) \
  ::s1::Log::Write((level), __FILE__, __LINE__, __VA_ARGS__)

#define S1_TRACE(...) S1_LOG(::s1::LogLevel::kTrace, __VA_ARGS__)
#define S1_DEBUG(...) S1_LOG(::s1::LogLevel::kDebug, __VA_ARGS__)
#define S1_INFO(...)  S1_LOG(::s1::LogLevel::kInfo, __VA_ARGS__)
#define S1_WARN(...)  S1_LOG(::s1::LogLevel::kWarn, __VA_ARGS__)
#define S1_ERROR(...) S1_LOG(::s1::LogLevel::kError, __VA_ARGS__)

#endif  // S1_LOG_H
