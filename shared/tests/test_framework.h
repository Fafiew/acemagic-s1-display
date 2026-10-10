// test_framework.h - tiny dependency-free test harness.
// License: GPL-3.0 (see LICENSE).
#ifndef S1_TEST_FRAMEWORK_H
#define S1_TEST_FRAMEWORK_H

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace s1test {

struct TestCase {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& Registry() {
  static std::vector<TestCase> r;
  return r;
}

inline int& Failures() {
  static int f = 0;
  return f;
}

inline const char*& Current() {
  static const char* c = "";
  return c;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) {
    Registry().push_back({name, std::move(fn)});
  }
};

inline void ReportFailure(const char* file, int line, const std::string& msg) {
  ++Failures();
  std::fprintf(stderr, "FAIL [%s] %s:%d: %s\n", Current(), file, line,
               msg.c_str());
}

inline int RunAll() {
  int passed = 0;
  for (auto& t : Registry()) {
    Current() = t.name;
    const int before = Failures();
    t.fn();
    if (Failures() == before) {
      ++passed;
      std::printf("ok   %s\n", t.name);
    }
  }
  std::printf("\n%d/%zu tests passed, %d assertion failure(s)\n", passed,
              Registry().size(), Failures());
  return Failures() == 0 ? 0 : 1;
}

}  // namespace s1test

#define TEST(name)                                                    \
  static void name();                                                 \
  static ::s1test::Registrar reg_##name(#name, name);                 \
  static void name()

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond))                                                      \
      ::s1test::ReportFailure(__FILE__, __LINE__, "CHECK(" #cond ")"); \
  } while (0)

#define CHECK_EQ(a, b)                                                \
  do {                                                                \
    auto _a = (a);                                                    \
    auto _b = (b);                                                    \
    if (!(_a == _b))                                                  \
      ::s1test::ReportFailure(__FILE__, __LINE__,                     \
                              "CHECK_EQ(" #a ", " #b ")");            \
  } while (0)

#endif  // S1_TEST_FRAMEWORK_H
