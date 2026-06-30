#ifndef CMONKEY_BASE_CORE_H
#define CMONKEY_BASE_CORE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// =============================================================
// internal keywords

#define internal      static
#define global        static
#define local_persist static
#define per_thread    _Thread_local

// =============================================================
// primitive types

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef size_t usize;
typedef ptrdiff_t isize;
typedef s8 b8;
typedef s16 b16;
typedef s32 b32;
typedef s64 b64;
typedef float f32;
typedef double f64;

// =============================================================
// primitive type functions

internal bool u8_is_digit(u8 ch);
internal bool u8_is_letter(u8 ch);
internal bool u8_is_space(u8 c);

internal usize usize_to_pow2(usize x);

// =============================================================
// helper macros

#define GLUE_(A, B) A##B
#define GLUE(A, B)  GLUE_(A, B)

#define min(a, b)      (((a) < (b)) ? (a) : (b))
#define max(a, b)      (((a) > (b)) ? (a) : (b))
#define clamp(a, x, b) (((x) < (a)) ? (a) : ((x) > (b)) ? (b) : (x))

#define ceil_pos(T, x) (((x) - (T)(x)) > 0 ? (T)((x) + 1) : (T)(x))
#define ceil_neg(T, x) (T)(x)
#define ceil(T, x)     (((x) > 0) ? ceil_pos(T, x) : ceil_neg(T, x))

#define align_up(p, a) (((usize)(p) + ((usize)(a) - 1)) & (~((usize)(a) - 1)))

#define arr_count(a) (sizeof(a) / sizeof(*a))

#define DEFER_LOOP(begin, end) for (int _i_ = ((begin), 0); !_i_; _i_ += 1, (end))

#if defined(__GNUC__) || defined(__clang__)
#  define ATTRIB_FMT(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else
#  define ATTRIB_FMT(fmt_idx, args_idx)
#endif

// =============================================================
// units

#define KiB(n) ((u64)(n) << 10)
#define MiB(n) ((u64)(n) << 20)
#define GiB(n) ((u64)(n) << 30)

// =============================================================
// assertions

#if defined(__GNUC__) || defined(__clang__)
#  define TRAP() __builtin_trap()
#elif defined(_MSC_VER)
#  define TRAP() __debugbreak()
#else
#  error Unknown trap intrinsic for this compiler.
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#  define STATICASSERT(C, ID) _Static_assert(C, #ID)
#else
#  define STATICASSERT(C, ID) typedef char GLUE(ID, __LINE__)[(!!(C)) * 2 - 1]
#endif

internal void assert_handler(char const *prefix,
                             char const *condition,
                             char const *file,
                             int line,
                             int column,
                             char const *msg,
                             ...) ATTRIB_FMT(6, 7);

#define assert(x)                                               \
  do                                                            \
  {                                                             \
    if (!(x))                                                   \
    {                                                           \
      assert_handler("error", #x, __FILE__, __LINE__, 0, NULL); \
      TRAP();                                                   \
    }                                                           \
  } while (0)

#define assert_m(x, msg, ...)                                                 \
  do                                                                          \
  {                                                                           \
    if (!(x))                                                                 \
    {                                                                         \
      assert_handler("error", #x, __FILE__, __LINE__, 0, msg, ##__VA_ARGS__); \
      TRAP();                                                                 \
    }                                                                         \
  } while (0)

#ifndef NDEBUG
#  define debug_assert(x)             assert(x)
#  define debug_assert_m(x, msg, ...) assert_m(x, msg, ##__VA_ARGS__)
#else
#  define debug_assert(x)             ((void)(x))
#  define debug_assert_m(x, msg, ...) ((void)(x))
#endif

#define PANIC(msg)      assert_m(0, msg)
#define INVALID_PATH    assert(0 && "Invalid Path")
#define NOT_IMPLEMENTED assert(0 && "Not Implemented")
#define NOOP            ((void)0)

// =============================================================
// linked lists

#define sll_append(t, n) \
  do                     \
  {                      \
    (t)->next = (n);     \
    (t)       = (n);     \
    (t)->next = 0;       \
  } while (0)

#define SLL_BUILDER(T) \
  struct               \
  {                    \
    T sentinel;        \
    T *tail;           \
    usize count;       \
  }

#define sll_builder_init(b)            \
  do                                   \
  {                                    \
    (b).tail          = &(b).sentinel; \
    (b).sentinel.next = 0;             \
    (b).count         = 0;             \
  } while (0)

#define sll_builder_append(b, n) \
  do                             \
  {                              \
    sll_append((b).tail, (n));   \
    (b).count++;                 \
  } while (0)

#define sll_builder_result(b) (b).sentinel.next

#endif // CMONKEY_BASE_CORE_H
