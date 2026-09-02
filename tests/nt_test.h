// =============================================================================
//  Pebblebol host tests - nt_test.h
//  Minimal single-header harness (plan §4.1). No external framework, no
//  Arduino headers, no allocation beyond what the tests themselves do.
//
//  Usage: one binary per test_*.cpp. Include this header once, then:
//
//      TEST(name) { CHECK(cond); CHECK_EQ(a, b); CHECK_NEAR(a, b, eps); }
//
//  main() is provided here (define NT_TEST_NO_MAIN to supply your own):
//      ./bin/test_x [--seed N] [--filter substr] [--list] [extra flags]
//  Extra flags are left for the test to query with nt_flag()/nt_opt()
//  (test_sim_golden uses --record). Every failed check prints file:line;
//  the process exits non-zero if any check failed or no test matched.
// =============================================================================
#ifndef NT_TEST_H
#define NT_TEST_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NT_TESTS_DIR
#define NT_TESTS_DIR "."
#endif

struct NtTestCase {
  const char* name;
  void (*fn)(void);
  NtTestCase* next;
};

struct NtTestState {
  NtTestCase* head;
  NtTestCase* tail;
  long        checks;        // checks evaluated so far (all tests)
  int         fails;         // failed checks in the test currently running
  int         argc;
  char**      argv;
  uint32_t    seed;          // --seed N, default 0xC0FFEE (plan §4.1)
  const char* filter;        // --filter substr
};

inline NtTestState& nt_state(void) {
  static NtTestState s = { nullptr, nullptr, 0, 0, 0, nullptr, 0xC0FFEEu, nullptr };
  return s;
}

struct NtTestReg {
  NtTestReg(const char* name, void (*fn)(void), NtTestCase* slot) {
    slot->name = name;
    slot->fn   = fn;
    slot->next = nullptr;
    NtTestState& s = nt_state();
    if (s.tail) s.tail->next = slot; else s.head = slot;
    s.tail = slot;
  }
};

#define TEST(name)                                                              \
  static void nt_test_fn_##name(void);                                          \
  static NtTestCase nt_test_case_##name;                                        \
  static NtTestReg  nt_test_reg_##name(#name, &nt_test_fn_##name,               \
                                       &nt_test_case_##name);                   \
  static void nt_test_fn_##name(void)

inline void nt_fail_at(const char* file, int line, const char* what) {
  nt_state().fails++;
  fprintf(stderr, "  %s:%d: FAIL %s\n", file, line, what);
}

#define CHECK(cond)                                                             \
  do {                                                                          \
    nt_state().checks++;                                                        \
    if (!(cond)) nt_fail_at(__FILE__, __LINE__, "CHECK(" #cond ")");           \
  } while (0)

// Integers, enums and bools; both sides are widened to long long so a
// signed/unsigned mix never trips -Wsign-compare inside the test files.
#define CHECK_EQ(a, b)                                                          \
  do {                                                                          \
    const long long nt_a_ = (long long)(a);                                     \
    const long long nt_b_ = (long long)(b);                                     \
    nt_state().checks++;                                                        \
    if (nt_a_ != nt_b_) {                                                       \
      char nt_msg_[256];                                                        \
      snprintf(nt_msg_, sizeof nt_msg_, "CHECK_EQ(%s, %s): %lld != %lld",       \
               #a, #b, nt_a_, nt_b_);                                           \
      nt_fail_at(__FILE__, __LINE__, nt_msg_);                                  \
    }                                                                           \
  } while (0)

// |a - b| <= eps, integer arithmetic only (the firmware has no FPU).
#define CHECK_NEAR(a, b, eps)                                                   \
  do {                                                                          \
    const long long nt_a_ = (long long)(a);                                     \
    const long long nt_b_ = (long long)(b);                                     \
    const long long nt_e_ = (long long)(eps);                                   \
    const long long nt_d_ = (nt_a_ > nt_b_) ? (nt_a_ - nt_b_) : (nt_b_ - nt_a_);\
    nt_state().checks++;                                                        \
    if (nt_d_ > nt_e_) {                                                        \
      char nt_msg_[256];                                                        \
      snprintf(nt_msg_, sizeof nt_msg_,                                         \
               "CHECK_NEAR(%s, %s, %s): %lld vs %lld (diff %lld > %lld)",       \
               #a, #b, #eps, nt_a_, nt_b_, nt_d_, nt_e_);                       \
      nt_fail_at(__FILE__, __LINE__, nt_msg_);                                  \
    }                                                                           \
  } while (0)

// NUL-terminated strings, byte-for-byte.
#define CHECK_STR_EQ(a, b)                                                      \
  do {                                                                          \
    const char* nt_a_ = (a);                                                    \
    const char* nt_b_ = (b);                                                    \
    nt_state().checks++;                                                        \
    if (nt_a_ == nullptr || nt_b_ == nullptr || strcmp(nt_a_, nt_b_) != 0) {   \
      char nt_msg_[512];                                                        \
      snprintf(nt_msg_, sizeof nt_msg_, "CHECK_STR_EQ(%s, %s): \"%s\" != \"%s\"",\
               #a, #b, nt_a_ ? nt_a_ : "(null)", nt_b_ ? nt_b_ : "(null)");    \
      nt_fail_at(__FILE__, __LINE__, nt_msg_);                                  \
    }                                                                           \
  } while (0)

// --seed N as parsed by main(); tests that need a seed and were given none
// use the plan's default 0xC0FFEE.
inline uint32_t nt_seed(void) { return nt_state().seed; }

// True if `flag` (e.g. "--record") appears anywhere on the command line.
inline bool nt_flag(const char* flag) {
  const NtTestState& s = nt_state();
  for (int i = 1; i < s.argc; i++) {
    if (strcmp(s.argv[i], flag) == 0) return true;
  }
  return false;
}

// Value following `opt` on the command line, or nullptr.
inline const char* nt_opt(const char* opt) {
  const NtTestState& s = nt_state();
  for (int i = 1; i + 1 < s.argc; i++) {
    if (strcmp(s.argv[i], opt) == 0) return s.argv[i + 1];
  }
  return nullptr;
}

// Joins NT_TESTS_DIR (the tests/ folder, baked in by the Makefile) with a
// relative path so a binary finds fixtures/ and golden/ from any cwd.
inline const char* nt_path(const char* rel, char* buf, size_t n) {
  snprintf(buf, n, "%s/%s", NT_TESTS_DIR, rel);
  return buf;
}

#ifndef NT_TEST_NO_MAIN
int main(int argc, char** argv) {
  NtTestState& s = nt_state();
  s.argc = argc;
  s.argv = argv;
  bool list_only = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      s.seed = (uint32_t)strtoul(argv[++i], nullptr, 0);
    } else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
      s.filter = argv[++i];
    } else if (strcmp(argv[i], "--list") == 0) {
      list_only = true;
    }
    // anything else is a test-specific flag, reachable through nt_flag()/nt_opt()
  }

  int ran = 0;
  int failed = 0;
  for (NtTestCase* t = s.head; t != nullptr; t = t->next) {
    if (s.filter != nullptr && strstr(t->name, s.filter) == nullptr) continue;
    if (list_only) { printf("%s\n", t->name); continue; }
    s.fails = 0;
    t->fn();
    ran++;
    if (s.fails) {
      failed++;
      printf("FAIL %s (%d failed check%s)\n", t->name, s.fails, s.fails == 1 ? "" : "s");
    } else {
      printf("ok   %s\n", t->name);
    }
  }
  if (list_only) return 0;
  if (ran == 0) {
    fprintf(stderr, "%s: no tests matched\n", argv[0]);
    return 1;
  }
  printf("%s: %d/%d tests passed, %ld checks, seed 0x%X\n",
         argv[0], ran - failed, ran, s.checks, (unsigned)s.seed);
  return failed ? 1 : 0;
}
#endif  // NT_TEST_NO_MAIN

#endif  // NT_TEST_H
