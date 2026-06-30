#ifndef TM_TEST_H
#define TM_TEST_H

#include <stdio.h>

// TODO(tad): Printing is fine for this toy program, but it'd be more generally useful if these
// assertion macros collected failure messages for general use. This would also make it easier
// to build failure stack traces (currently handled by printing).

#if defined(_WIN32) || defined(_WIN64)
#  include <io.h>
#  define is_term(stream) _isatty(_fileno(stream))
#else
#  include <unistd.h>
#  define is_term(stream) isatty(fileno(stream))
#endif

#define test_fail(fmt, ...) test_assert_m(false, fmt, ##__VA_ARGS__)
#define test_assert(test)   test_assert_m(test, "test error")

#define test_assert_m(test, fmt, ...)            \
  do                                             \
  {                                              \
    if (!(test))                                 \
    {                                            \
      printf("%s:%d: ", __FILE__, __LINE__);     \
      if (is_term(stdout)) printf("\033[0;31m"); \
      printf("failure in %s:", __func__);        \
      if (is_term(stdout)) printf("\033[0m");    \
      printf(" " fmt "\n", ##__VA_ARGS__);       \
      return 1;                                  \
    }                                            \
  } while (0)

#define test_helper(help)                                    \
  do                                                         \
  {                                                          \
    if (help)                                                \
    {                                                        \
      printf("| %s:%d: %s\n", __FILE__, __LINE__, __func__); \
      return 1;                                              \
    }                                                        \
  } while (0)

#endif // TM_TEST_H
