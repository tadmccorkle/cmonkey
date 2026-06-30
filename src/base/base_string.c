#include "base_string.h"

// =============================================================
// strings

internal Str8
str8(u8 const *value, usize length)
{
  return (Str8){ value, length };
}

internal Str8
str8_from_cstr(char const *c)
{
  return str8((u8 const *)c, strlen(c));
}

internal Str8
str8_fv(Arena *arena, char const *fmt, va_list args)
{
  va_list args2;
  va_copy(args2, args);
  int required_len = vsnprintf(0, 0, fmt, args);
  if (required_len < 0)
  {
    debug_assert_m(0, "Failed to determine required length for formatted Str8.");
    return (Str8){ 0 };
  }
  usize len = (usize)required_len;
  char *buf = arena_alloc_tn_nz(arena, char, len + 1);
  vsnprintf(buf, len + 1, fmt, args2);
  buf[len] = 0;
  va_end(args2);
  return str8((u8 *)buf, len);
}

internal Str8
str8_f(Arena *arena, char const *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  Str8 result = str8_fv(arena, fmt, args);
  va_end(args);
  return result;
}

internal Str8
str8_copy(Arena *arena, Str8 s)
{
  u8 *buf    = arena_alloc_tn_nz(arena, u8, s.len + 1);
  buf[s.len] = 0;

  memcpy(buf, s.buf, s.len);

  return str8(buf, s.len);
}

internal Str8
str8_concat(Arena *arena, Str8 s1, Str8 s2)
{
  usize len = s1.len + s2.len;
  u8 *buf   = arena_alloc_tn_nz(arena, u8, len + 1);
  buf[len]  = 0;

  memcpy(buf, s1.buf, s1.len);
  memcpy(buf + s1.len, s2.buf, s2.len);

  return str8(buf, len);
}

internal bool
str8_equal(Str8 a, Str8 b)
{
  return a.len == b.len && (a.buf == b.buf || a.len == 0 || memcmp(a.buf, b.buf, a.len) == 0);
}

internal Str8
str8_trim_l(Str8 s)
{
  if (s.len > 0)
  {
    usize trimmed = 0;
    while (trimmed < s.len && u8_is_space(s.buf[trimmed]))
    {
      trimmed++;
    }
    s.buf += trimmed;
    s.len -= trimmed;
  }

  return s;
}

internal Str8
str8_trim_r(Str8 s)
{
  if (s.len > 0)
  {
    usize trimmed = 0;
    usize i       = s.len;
    while (i > 0 && u8_is_space(s.buf[i - 1]))
    {
      i--;
      trimmed++;
    }
    s.len -= trimmed;
  }

  return s;
}

internal Str8
str8_trim(Str8 s)
{
  return str8_trim_l(str8_trim_r(s));
}

internal Str8
str8_slice(Str8 s, usize start, usize end)
{
  start = min(start, s.len);
  end   = min(end, s.len);
  end   = max(start, end);

  s.buf += start;
  s.len = end - start;

  return s;
}

internal Str8
str8_prefix(Str8 s, usize len)
{
  s.len = min(len, s.len);
  return s;
}

internal Str8
str8_suffix(Str8 s, usize len)
{
  len = min(len, s.len);
  s.buf += s.len - len;
  s.len = len;
  return s;
}

internal Str8
str8_skip(Str8 s, usize count)
{
  count = min(count, s.len);
  s.buf += count;
  s.len -= count;
  return s;
}

internal Str8
str8_chop(Str8 s, usize count)
{
  s.len -= min(count, s.len);
  return s;
}

// =============================================================
// string builders

internal StrBuilder8
str8_builder_create(Arena *arena, usize capacity)
{
  StrBuilder8 builder = { .arena = arena, .cap = capacity };
  builder.buf         = arena_alloc_tn(arena, u8, capacity);
  return builder;
}

internal void
str8_builder_reset(StrBuilder8 *builder)
{
  builder->len = 0;
}

internal void
str8_ensure_capacity(StrBuilder8 *builder, usize required)
{
  if (builder->cap < required)
  {
    usize old_cap = builder->cap;
    usize new_cap = max(2 * old_cap, required);
    builder->buf  = arena_realloc_t(builder->arena, builder->buf, u8, builder->cap, new_cap);
    builder->cap  = new_cap;
  }
}

internal void
str8_append(StrBuilder8 *builder, Str8 value)
{
  str8_ensure_capacity(builder, builder->len + value.len);
  memcpy(builder->buf + builder->len, value.buf, value.len);
  builder->len += value.len;
}

internal void
str8_append_char(StrBuilder8 *builder, u8 value)
{
  str8_ensure_capacity(builder, builder->len + 1);
  builder->buf[builder->len] = value;
  builder->len += 1;
}

internal void
str8_append_fv(StrBuilder8 *builder, char const *fmt, va_list args)
{
  va_list args2;
  va_copy(args2, args);
  int required_len = vsnprintf(0, 0, fmt, args);
  if (required_len < 0)
  {
    debug_assert_m(0, "Failed to determine required length for Str8 append value.");
    return;
  }
  usize append_len = (usize)required_len;
  str8_ensure_capacity(builder, builder->len + append_len + 1);
  vsnprintf((char *)builder->buf + builder->len, append_len + 1, fmt, args2);
  builder->len += append_len;
  va_end(args2);
}

internal void
str8_append_f(StrBuilder8 *builder, char const *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  str8_append_fv(builder, fmt, args);
  va_end(args);
}

internal Str8
str8_build(StrBuilder8 *builder)
{
  str8_ensure_capacity(builder, builder->len + 1);
  builder->buf[builder->len] = 0;

  Str8 result = str8(builder->buf, builder->len);
  *builder    = (StrBuilder8){ 0 };

  return result;
}

internal Str8
str8_dump(StrBuilder8 *builder, Arena *arena)
{
  u8 *buf = arena_alloc_tn_nz(arena, u8, builder->len + 1);
  memcpy(buf, builder->buf, builder->len);
  buf[builder->len] = 0;

  return str8(buf, builder->len);
}
