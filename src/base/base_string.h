#ifndef CMONKEY_BASE_STRING_H
#define CMONKEY_BASE_STRING_H

#include "base_core.h"
#include "base_arena.h"

// =============================================================
// strings

typedef struct Str8 Str8;
struct Str8
{
  u8 const *buf;
  usize len;
};

#define str8_lit(S) (Str8){ (u8 *)(S), sizeof(S) - 1 }
#define str8_cti(S) { (u8 *)(S), sizeof(S) - 1 }
#define str8_va(S)  (int)(S).len, (S).buf

internal Str8 str8(u8 const *value, usize length);

internal Str8 str8_from_cstr(char const *c);

internal Str8 str8_fv(Arena *arena, char const *fmt, va_list args) ATTRIB_FMT(2, 0);
internal Str8 str8_f(Arena *arena, char const *fmt, ...) ATTRIB_FMT(2, 3);
internal Str8 str8_copy(Arena *arena, Str8 s);
internal Str8 str8_concat(Arena *arena, Str8 s1, Str8 s2);

internal bool str8_equal(Str8 a, Str8 b);

internal Str8 str8_trim_l(Str8 s);
internal Str8 str8_trim_r(Str8 s);
internal Str8 str8_trim(Str8 s);

internal Str8 str8_slice(Str8 s, usize start, usize end);
internal Str8 str8_prefix(Str8 s, usize len);
internal Str8 str8_suffix(Str8 s, usize len);
internal Str8 str8_skip(Str8 s, usize count);
internal Str8 str8_chop(Str8 s, usize count);

// =============================================================
// string builders

typedef struct StrBuilder8 StrBuilder8;
struct StrBuilder8
{
  u8 *buf;
  usize len;
  usize cap;
  Arena *arena;
};

internal StrBuilder8 str8_builder_create(Arena *arena, usize capacity);
internal void str8_builder_reset(StrBuilder8 *builder);
internal void str8_ensure_capacity(StrBuilder8 *builder, usize required);
internal void str8_append(StrBuilder8 *builder, Str8 value);
internal void str8_append_char(StrBuilder8 *builder, u8 value);
internal void str8_append_fv(StrBuilder8 *builder, char const *fmt, va_list args) ATTRIB_FMT(2, 0);
internal void str8_append_f(StrBuilder8 *builder, char const *fmt, ...) ATTRIB_FMT(2, 3);
internal Str8 str8_build(StrBuilder8 *builder);
internal Str8 str8_dump(StrBuilder8 *builder, Arena *arena);

#define STR8_BUILDER_DEFAULT_CAPACITY 256

#define str8_builder_create_default(arena) \
  str8_builder_create((arena), STR8_BUILDER_DEFAULT_CAPACITY)
#define str8_append_lit(builder, value) str8_append((builder), str8_lit(value))

#endif // CMONKEY_BASE_STRING_H
