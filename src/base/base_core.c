#include "base_core.h"

internal bool
u8_is_digit(u8 ch)
{
  return '0' <= ch && ch <= '9';
}

internal bool
u8_is_letter(u8 ch)
{
  return ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z');
}

internal bool
u8_is_space(u8 c)
{
  return c == ' ' || c - '\t' < 5;
}

internal usize
usize_to_pow2(usize x)
{
  if (x == 0) return 1;

  assert(x < ((usize)1 << (sizeof(usize) * 8 - 1)));

  x -= 1;

#if defined(__GNUC__) || defined(__clang__)
#  if UINTPTR_MAX > 0xFFFFFFFF
  return (usize)1 << (64 - __builtin_clzll((unsigned long long)x));
#  else
  return (usize)1 << (32 - __builtin_clzl((unsigned long)x));
#  endif
#else
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
#  if UINTPTR_MAX > 0xFFFFFFFF
  x |= x >> 32;
#  endif
  return x + 1;
#endif
}

internal void
assert_handler(const char *prefix,
               const char *condition,
               const char *file,
               int line,
               int column,
               const char *msg,
               ...)
{
  fprintf(stderr, "%s:%d:", file, line);
  if (column > 0) fprintf(stderr, "%d:", column);
  fprintf(stderr, " %s: ", prefix);

  if (condition)
  {
    fprintf(stderr, "`%s` - ", condition);
  }

  if (msg)
  {
    va_list va;
    va_start(va, msg);
    vfprintf(stderr, msg, va);
    va_end(va);
  }

  fprintf(stderr, "\n");
  fflush(stderr);
}
