#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
// std

#define internal      static
#define global        static
#define local_persist static
#define per_thread    _Thread_local

#define KiB(n) ((u64)(n) << 10)
#define MiB(n) ((u64)(n) << 20)
#define GiB(n) ((u64)(n) << 30)

#define min(a, b)      (((a) < (b)) ? (a) : (b))
#define max(a, b)      (((a) > (b)) ? (a) : (b))
#define clamp(a, x, b) (((x) < (a)) ? (a) : ((x) > (b)) ? (b) : (x))

#define ceil_pos(T, x) ((x - (T)(x)) > 0 ? (T)(x + 1) : (T)(x))
#define ceil_neg(T, x) (T)(x)
#define ceil(T, x)     (((x) > 0) ? ceil_pos(x) : ceil_neg(x))

#define align_up(p, a) (((usize)(p) + ((usize)(a) - 1)) & (~((usize)(a) - 1)))

#if defined(__GNUC__) || defined(__clang__)
#  if UINTPTR_MAX == 0xFFFFFFFF
#    define next_pow2(x) x <= 0 ? 2 : ((usize)1 << (32 - __builtin_clz((usize)(x) - 1)))
#  else
#    define next_pow2(x) x <= 0 ? 2 : ((usize)1 << (64 - __builtin_clzl((usize)(x) - 1)))
#  endif
#elif defined(_MSC_VER)
#  include <intrin.h>
#  if UINTPTR_MAX == 0xFFFFFFFF
#    define next_pow2(x) x == 0 ? 1 : ((usize)1 << (32 - __lzcnt((usize)(x) - 1)))
#  else
#    define next_pow2(x) x == 0 ? 1 : ((usize)1 << (64 - __lzcnt64((usize)(x) - 1)))
#  endif
#endif

#define arr_count(a) sizeof(a) / sizeof(*a)

#define DEFER_LOOP(begin, end) for (int _i_ = ((begin), 0); !_i_; _i_ += 1, (end))

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

#if defined(__GNUC__) || defined(__clang__)
#  define ATTRIB_FMT(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else
#  define ATTRIB_FMT(fmt_idx, args_idx)
#endif

////////////////////////////////////////////////////////////////////////////////
// ll

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

////////////////////////////////////////////////////////////////////////////////
// arena

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"

#define ARENA_DEFAULT_SIZE MiB(2)

typedef enum
{
  ArenaOpt_None   = 0,
  ArenaOpt_NoZero = 1 << 0,
} ArenaOpt;

typedef struct ArenaBlock ArenaBlock;
struct ArenaBlock
{
  ArenaBlock *prev;
  usize size;
  usize pos;
  u8 data[];
};

typedef struct Arena Arena;
struct Arena
{
  ArenaBlock *current;
};

typedef struct TmpArena TmpArena;
struct TmpArena
{
  Arena *arena;
  ArenaBlock *block;
  usize pos;
};

internal ArenaBlock *
arena_block_create(usize size)
{
  // TODO(tad): Check/handle failure or assert.
  ArenaBlock *block = malloc(sizeof(ArenaBlock) + size);
  block->prev       = 0;
  block->size       = size;
  block->pos        = 0;
  return block;
}

internal Arena *
arena_create(usize size)
{
  // TODO(tad): Check/handle failure or assert.
  Arena *arena   = malloc(sizeof(Arena));
  arena->current = arena_block_create(size);
  return arena;
}

internal void *
arena_alloc(Arena *arena, usize size, usize align, ArenaOpt opts)
{
  ArenaBlock *block = arena->current;
  usize pos         = align_up(block->pos, align);

  if (pos + size > block->size)
  {
    usize new_size = block->size * 2;
    if (size > new_size)
    {
      new_size = size;
    }

    // TODO(tad): remove print
    printf("Creating new arena block\n");
    ArenaBlock *new_block = arena_block_create(new_size);
    new_block->prev       = block;
    arena->current        = new_block;
    block                 = new_block;
    pos                   = 0;
  }

  void *result = block->data + pos;
  block->pos   = pos + size;

  if (!(opts & ArenaOpt_NoZero))
  {
    memset(result, 0, size);
  }

  return result;
}

internal void *
arena_realloc(Arena *arena, void *ptr, usize old_size, usize new_size, usize align, ArenaOpt opts)
{
  if (ptr == 0 || old_size == 0) return arena_alloc(arena, new_size, align, opts);

  ArenaBlock *block = arena->current;

  if ((u8 *)ptr + old_size == block->data + block->pos)
  {
    if (new_size <= old_size)
    {
      block->pos -= old_size - new_size;
      return ptr;
    }

    usize required = new_size - old_size;
    if (block->pos + required <= block->size)
    {
      if (!(opts & ArenaOpt_NoZero))
      {
        memset((u8 *)ptr + old_size, 0, required);
      }
      block->pos += required;
      return ptr;
    }
  }

  void *result = arena_alloc(arena, new_size, align, opts);
  memcpy(result, ptr, min(old_size, new_size));
  return result;
}

#define arena_alloc_tn(arena, T, n) \
  (T *)arena_alloc((arena), sizeof(T) * (n), _Alignof(T), ArenaOpt_None)
#define arena_alloc_tn_nz(arena, T, n) \
  (T *)arena_alloc((arena), sizeof(T) * (n), _Alignof(T), ArenaOpt_NoZero)

#define arena_alloc_t(arena, T)    arena_alloc_tn((arena), T, 1)
#define arena_alloc_t_nz(arena, T) arena_alloc_tn_nz((arena), T, 1)

#define arena_realloc_t(arena, ptr, T, old_n, new_n) \
  (T *)arena_realloc((arena), (ptr), sizeof(T) * (old_n), sizeof(T) * (new_n), _Alignof(T), ArenaOpt_None)
#define arena_realloc_t_nz(arena, ptr, T, old_n, new_n) \
  (T *)arena_realloc((arena), (ptr), sizeof(T) * (old_n), sizeof(T) * (new_n), _Alignof(T), ArenaOpt_NoZero)

internal void
arena_free(Arena *arena)
{
  ArenaBlock *block = arena->current;
  while (block)
  {
    ArenaBlock *prev = block->prev;
    free(block);
    block = prev;
  }
  free(arena);
}

internal TmpArena
arena_tmp_begin(Arena *arena)
{
  TmpArena tmp;
  tmp.arena = arena;
  tmp.block = arena->current;
  tmp.pos   = arena->current->pos;
  return tmp;
}

internal void
arena_tmp_commit(TmpArena *tmp)
{
  tmp->block = tmp->arena->current;
  tmp->pos   = tmp->arena->current->pos;
}

internal void
arena_tmp_end(TmpArena tmp)
{
  if (tmp.arena == 0) return;

  Arena *arena = tmp.arena;

  while (arena->current != tmp.block)
  {
    ArenaBlock *block = arena->current;
    arena->current    = block->prev;
    free(block);
  }

  arena->current->pos = tmp.pos;
}

#define SCRATCH_ARENA_COUNT 2

internal per_thread Arena *tl_scratch_arenas[SCRATCH_ARENA_COUNT];

internal Arena *
tl_scratch_arena_get(Arena **conflicts, usize conflict_count)
{
  if (!tl_scratch_arenas[0])
  {
    for (usize i = 0; i < SCRATCH_ARENA_COUNT; i++)
    {
      tl_scratch_arenas[i] = arena_create(ARENA_DEFAULT_SIZE);
    }
  }

  for (usize i = 0; i < SCRATCH_ARENA_COUNT; i++)
  {
    Arena *candidate = tl_scratch_arenas[i];

    bool is_conflict = false;
    for (usize j = 0; j < conflict_count; j++)
    {
      if (conflicts[j] == candidate)
      {
        is_conflict = true;
        break;
      }
    }

    if (!is_conflict) return candidate;
  }

  assert(0 && "No non-conflicting scratch arena available.");
  return 0;
}

#define scratch_begin(...)                                         \
  arena_tmp_begin(tl_scratch_arena_get((Arena *[]){ __VA_ARGS__ }, \
                                       sizeof((Arena *[]){ __VA_ARGS__ }) / sizeof(Arena *)))

#define scratch_end(scratch) arena_tmp_end(scratch)

#pragma clang diagnostic pop

////////////////////////////////////////////////////////////////////////////////
// strings

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"

typedef struct Str8 Str8;
struct Str8
{
  u8 const *buf;
  usize len;
};

#define str8_lit(S) (Str8){ (u8 *)(S), sizeof(S) - 1 }
#define str8_cti(S) { (u8 *)(S), sizeof(S) - 1 }
#define str8_va(S)  (int)(S).len, (S).buf

internal Str8
str8(u8 const *value, usize length)
{
  return (Str8){ value, length };
}

internal Str8 str8_fv(Arena *arena, char const *fmt, va_list args) ATTRIB_FMT(2, 0);
internal Str8 str8_f(Arena *arena, char const *fmt, ...) ATTRIB_FMT(2, 3);

internal Str8
str8_fv(Arena *arena, char const *fmt, va_list args)
{
  va_list args2;
  va_copy(args2, args);
  int required_len = vsnprintf(0, 0, fmt, args);
  if (required_len < 0)
  {
    // TODO(tad): Handle failure? Return zero Str8?
    assert(0 && "Failed to determine required length for formatted Str8.");
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
str8_from_cstr(char const *c)
{
  return str8((u8 const *)c, strlen(c));
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
  return a.len == b.len && (a.len == 0 || memcmp(a.buf, b.buf, a.len) == 0);
}

#define STR8_BUILDER_DEFAULT_CAPACITY 256

typedef struct StrBuilder8 StrBuilder8;
struct StrBuilder8
{
  u8 *buf;
  usize len;
  usize cap;
  Arena *arena;
};

internal StrBuilder8
str8_builder_create(Arena *arena, usize capacity)
{
  StrBuilder8 builder = { .arena = arena, .cap = capacity };
  builder.buf         = arena_alloc_tn(arena, u8, capacity);
  return builder;
}

#define str8_builder_create_default(arena) \
  str8_builder_create((arena), STR8_BUILDER_DEFAULT_CAPACITY)

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

#define str8_append_lit(builder, value) str8_append((builder), str8_lit(value))

internal void str8_append_fv(StrBuilder8 *builder, char const *fmt, va_list args) ATTRIB_FMT(2, 0);
internal void str8_append_f(StrBuilder8 *builder, char const *fmt, ...) ATTRIB_FMT(2, 3);

internal void
str8_append_fv(StrBuilder8 *builder, char const *fmt, va_list args)
{
  va_list args2;
  va_copy(args2, args);
  int required_len = vsnprintf(0, 0, fmt, args);
  if (required_len < 0)
  {
    // TODO(tad): Handle failure? Append nothing or token value?
    assert(0 && "Failed to determine required length for Str8 append value.");
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

#pragma clang diagnostic pop

////////////////////////////////////////////////////////////////////////////////
// lex

internal bool
is_whitespace(u8 ch)
{
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\v' || ch == '\f';
}

internal bool
is_digit(u8 ch)
{
  return '0' <= ch && ch <= '9';
}

internal bool
is_letter(u8 ch)
{
  return ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z');
}

internal bool
is_escapable_char(u8 ch)
{
  // TODO(tad): Is this function even useful right now?
  return ch == '\\' || ch == '"' || ch == 't' || ch == 'n';
}

// TODO(tad): Use ASCII values for corresponding token kind values?
#define TOKEN_KINDS(X)         \
  X(Illegal)                   \
  X(EOF)                       \
                               \
  /* identifiers & literals */ \
  X(Identifier)                \
  X(Number)                    \
  X(String)                    \
  X(StringEsc)                 \
  X(StringOpen)                \
                               \
  /* operators */              \
  X(Assign)                    \
  X(Plus)                      \
  X(Minus)                     \
  X(Bang)                      \
  X(Star)                      \
  X(Slash)                     \
  X(Less)                      \
  X(Greater)                   \
  X(Equal)                     \
  X(NotEqual)                  \
                               \
  /* delimiters */             \
  X(Comma)                     \
  X(Semicolon)                 \
  X(LParen)                    \
  X(RParen)                    \
  X(LBrace)                    \
  X(RBrace)                    \
  X(LBracket)                  \
  X(RBracket)                  \
                               \
  /* keywords */               \
  X(Function)                  \
  X(Let)                       \
  X(True)                      \
  X(False)                     \
  X(If)                        \
  X(Else)                      \
  X(Return)

typedef enum
{
#define TOKEN_KIND(name) TokenKind_##name,
  TOKEN_KINDS(TOKEN_KIND)
#undef TOKEN_KIND
} TokenKind;

internal Str8
token_name(TokenKind kind)
{
  switch (kind)
  {
#define TOKEN_KIND(name) \
  case TokenKind_##name: return str8_lit(#name);
    TOKEN_KINDS(TOKEN_KIND)
#undef TOKEN_KIND
    default: return str8_lit("Unknown Token Kind");
  }
}

typedef struct Token Token;
struct Token
{
  TokenKind kind;
  Str8 value;
};

internal const Str8 KEYWORD_FUNCTION = str8_lit("fn");
internal const Str8 KEYWORD_LET      = str8_lit("let");
internal const Str8 KEYWORD_TRUE     = str8_lit("true");
internal const Str8 KEYWORD_FALSE    = str8_lit("false");
internal const Str8 KEYWORD_IF       = str8_lit("if");
internal const Str8 KEYWORD_ELSE     = str8_lit("else");
internal const Str8 KEYWORD_RETURN   = str8_lit("return");

internal Token keyword_tokens[] = {
  { TokenKind_Function, KEYWORD_FUNCTION },
  { TokenKind_Let, KEYWORD_LET },
  { TokenKind_True, KEYWORD_TRUE },
  { TokenKind_False, KEYWORD_FALSE },
  { TokenKind_If, KEYWORD_IF },
  { TokenKind_Else, KEYWORD_ELSE },
  { TokenKind_Return, KEYWORD_RETURN },
};

typedef struct Lexer Lexer;
struct Lexer
{
  Str8 input;
  usize pos;
};

internal void
lex_init(Lexer *l, Str8 input)
{
  l->input = input;
  l->pos   = 0;
}

internal void
lex_consume_whitespace(Lexer *l)
{
  while (l->pos < l->input.len && is_whitespace(l->input.buf[l->pos]))
  {
    l->pos += 1;
  }
}

internal u8
lex_peek_char(Lexer const *l)
{
  usize next_pos = l->pos + 1;
  if (next_pos >= l->input.len)
  {
    return 0;
  }
  return l->input.buf[next_pos];
}

internal void
lex_init_token(Lexer *l, Token *token, usize length, TokenKind kind)
{
  token->value = str8(&l->input.buf[l->pos], length);
  token->kind  = kind;
  l->pos += length;
}

internal void
lex_advance_token(Lexer *l, Token *token)
{
  lex_consume_whitespace(l);

  if (l->pos >= l->input.len)
  {
    lex_init_token(l, token, 0, TokenKind_EOF);
    return;
  }

  u8 ch = l->input.buf[l->pos];
  switch (ch)
  {
    case '=':
    {
      if (lex_peek_char(l) == '=')
      {
        lex_init_token(l, token, 2, TokenKind_Equal);
      }
      else
      {
        lex_init_token(l, token, 1, TokenKind_Assign);
      }
      break;
    }
    case '!':
    {
      if (lex_peek_char(l) == '=')
      {
        lex_init_token(l, token, 2, TokenKind_NotEqual);
      }
      else
      {
        lex_init_token(l, token, 1, TokenKind_Bang);
      }
      break;
    }
    case '+': lex_init_token(l, token, 1, TokenKind_Plus); break;
    case '-': lex_init_token(l, token, 1, TokenKind_Minus); break;
    case '*': lex_init_token(l, token, 1, TokenKind_Star); break;
    case '/': lex_init_token(l, token, 1, TokenKind_Slash); break;
    case '<': lex_init_token(l, token, 1, TokenKind_Less); break;
    case '>': lex_init_token(l, token, 1, TokenKind_Greater); break;
    case ';': lex_init_token(l, token, 1, TokenKind_Semicolon); break;
    case '(': lex_init_token(l, token, 1, TokenKind_LParen); break;
    case ')': lex_init_token(l, token, 1, TokenKind_RParen); break;
    case '{': lex_init_token(l, token, 1, TokenKind_LBrace); break;
    case '}': lex_init_token(l, token, 1, TokenKind_RBrace); break;
    case '[': lex_init_token(l, token, 1, TokenKind_LBracket); break;
    case ']': lex_init_token(l, token, 1, TokenKind_RBracket); break;
    case ',': lex_init_token(l, token, 1, TokenKind_Comma); break;

    case '"':
    {
      usize pos;
      bool has_escaped_chars = false;
      for (pos = l->pos + 1; pos < l->input.len && l->input.buf[pos] != '"'; pos++)
      {
        if (l->input.buf[pos] == '\\')
        {
          usize escaped_pos = pos + 1;
          if (escaped_pos < l->input.len && is_escapable_char(l->input.buf[escaped_pos]))
          {
            pos               = escaped_pos;
            has_escaped_chars = true;
          }
        }
      }

      TokenKind string_kind;
      if (pos < l->input.len)
      {
        string_kind = has_escaped_chars ? TokenKind_StringEsc : TokenKind_String;
        pos += 1;
      }
      else
      {
        string_kind = TokenKind_StringOpen;
      }

      lex_init_token(l, token, pos - l->pos, string_kind);
      break;
    }

    default:
    {
      if (is_digit(ch))
      {
        usize len = 1;
        while (l->pos + len < l->input.len && is_digit(l->input.buf[l->pos + len]))
        {
          len += 1;
        }

        lex_init_token(l, token, len, TokenKind_Number);
      }
      else if (ch == '_' || is_letter(ch))
      {
        usize len = 1;
        while (l->pos + len < l->input.len)
        {
          ch = l->input.buf[l->pos + len];
          if (ch != '_' && !is_letter(ch) && !is_digit(ch)) break;
          len += 1;
        }

        TokenKind kind = TokenKind_Identifier;
        for (usize i = 0; i < arr_count(keyword_tokens); i++)
        {
          Str8 keyword = keyword_tokens[i].value;

          if (keyword.len != len) continue;

          if (!memcmp(keyword.buf, &l->input.buf[l->pos], keyword.len))
          {
            kind = keyword_tokens[i].kind;
          }
        }

        lex_init_token(l, token, len, kind);
      }
      else
      {
        lex_init_token(l, token, 1, TokenKind_Illegal);
      }
      break;
    }
  }
}

internal void
lex_print(Str8 input)
{
  Lexer l     = { 0 };
  Token token = { 0 };
  lex_init(&l, input);

  printf("id %-15s %s\n", "token", "value");
  do
  {
    lex_advance_token(&l, &token);
    printf("%2u %-15.*s %.*s\n", token.kind, str8_va(token_name(token.kind)), str8_va(token.value));
  } while (token.kind != TokenKind_EOF);
}

////////////////////////////////////////////////////////////////////////////////
// parse

typedef enum
{
  MessageLevel_None,
  MessageLevel_Trace,
  MessageLevel_Info,
  MessageLevel_Warn,
  MessageLevel_Error,
  MessageLevel_Critical,
} MessageLevel;

internal Str8
message_level_name(MessageLevel level)
{
  switch (level)
  {
    case MessageLevel_None: return (Str8){ 0 };
    case MessageLevel_Trace: return str8_lit("TRACE");
    case MessageLevel_Info: return str8_lit("INFO");
    case MessageLevel_Warn: return str8_lit("WARNING");
    case MessageLevel_Error: return str8_lit("ERROR");
    case MessageLevel_Critical: return str8_lit("CRITICAL");
    default: return str8_lit("Unknown Message Level");
  }
}

typedef struct Message Message;
struct Message
{
  MessageLevel level;
  Str8 value;

  Message *next;
};

typedef struct MessageList MessageList;
struct MessageList
{
  Message *first;
  Message *last;
  usize count;
  MessageLevel level;
};

typedef enum
{
  Precedence_Lowest,
  Precedence_Equals,      // ==
  Precedence_LessGreater, // > <
  Precedence_Sum,         // +
  Precedence_Product,     // *
  Precedence_Prefix,      // - !
  Precedence_Call,        // func()
  Precedence_Index,       // x[0]
} Precedence;

#define AST_EXPR_KINDS(X) \
  X(Identifier)           \
  X(Number)               \
  X(Boolean)              \
  X(String)               \
  X(Array)                \
  X(Prefix)               \
  X(Infix)                \
  X(IfElse)               \
  X(Function)             \
  X(Call)                 \
  X(Index)

typedef enum
{
#define AST_EXPR_KIND(name) AstExpr_##name,
  AST_EXPR_KINDS(AST_EXPR_KIND)
#undef AST_EXPR_KIND
} AstExprKind;

internal Str8
ast_expr_name(AstExprKind kind)
{
  switch (kind)
  {
#define AST_EXPR_KIND(name) \
  case AstExpr_##name: return str8_lit(#name);
    AST_EXPR_KINDS(AST_EXPR_KIND)
#undef AST_EXPR_KIND
    default: return str8_lit("Unknown AST Expression Kind");
  }
}

typedef struct AstStmt AstStmt;
typedef struct AstExpr AstExpr;

typedef struct AstStmtBlock AstStmtBlock;
struct AstStmtBlock
{
  Token token; // The '{' token
  AstStmt *statements;
};

typedef struct AstExprIdentifier AstExprIdentifier;
struct AstExprIdentifier
{
  Token token; // The identifier token
};

typedef struct AstExprNumber AstExprNumber;
struct AstExprNumber
{
  Token token; // The integer number token
  s64 value;
};

typedef struct AstExprBoolean AstExprBoolean;
struct AstExprBoolean
{
  Token token; // The boolean token
  b8 value;
};

typedef struct AstExprString AstExprString;
struct AstExprString
{
  Token token; // The string token
  Str8 value;
};

typedef struct AstExprArrayElement AstExprArrayElement;
struct AstExprArrayElement
{
  AstExprArrayElement *next;
  AstExpr *value;
};

typedef struct AstExprArray AstExprArray;
struct AstExprArray
{
  Token token; // The '[' token
  AstExprArrayElement *elements;
  usize count;
};

typedef struct AstExprPrefix AstExprPrefix;
struct AstExprPrefix
{
  Token token; // The prefix token, e.g., '!'
  AstExpr *rhs;
};

typedef struct AstExprInfix AstExprInfix;
struct AstExprInfix
{
  Token token; // The infix operator token, e.g., '+'
  AstExpr *lhs;
  AstExpr *rhs;
};

typedef struct AstExprIfElse AstExprIfElse;
struct AstExprIfElse
{
  Token token; // The 'if' token
  AstExpr *condition;
  AstStmtBlock *consequence;
  AstStmtBlock *alternative;
};

typedef struct AstExprFunctionParam AstExprFunctionParam;
struct AstExprFunctionParam
{
  AstExprFunctionParam *next;
  AstExprIdentifier identifier;
};

typedef struct AstExprFunction AstExprFunction;
struct AstExprFunction
{
  Token token; // The 'fn' token
  AstExprFunctionParam *params;
  u8 param_count;
  AstStmtBlock *body;
};

typedef struct AstExprCallArg AstExprCallArg;
struct AstExprCallArg
{
  AstExprCallArg *next;
  AstExpr *expr;
};

typedef struct AstExprCall AstExprCall;
struct AstExprCall
{
  Token token; // The '(' token
  AstExpr *function;
  AstExprCallArg *args;
  u8 arg_count;
};

typedef struct AstExprIndex AstExprIndex;
struct AstExprIndex
{
  Token token; // The '[' token
  AstExpr *lhs;
  AstExpr *index;
};

struct AstExpr
{
  AstExprKind tag;
  union
  {
    AstExprIdentifier identifier;
    AstExprNumber number;
    AstExprBoolean boolean;
    AstExprString string;
    AstExprArray array;
    AstExprPrefix prefix;
    AstExprInfix infix;
    AstExprIfElse if_else;
    AstExprFunction function;
    AstExprCall call;
    AstExprIndex index;
  } data;
};

typedef enum
{
  AstStmt_Let,
  AstStmt_Ret,
  AstStmt_Expr,
} AstStmtKind;

typedef struct AstStmtLet AstStmtLet;
struct AstStmtLet
{
  Token token; // The 'let' token
  AstExprIdentifier *identifier;
  AstExpr *expr;
};

typedef struct AstStmtReturn AstStmtReturn;
struct AstStmtReturn
{
  Token token; // The 'return' token
  AstExpr *expr;
};

typedef struct AstStmtExpr AstStmtExpr;
struct AstStmtExpr
{
  Token token; // The first token of the expression
  AstExpr *expr;
};

struct AstStmt
{
  AstStmt *next;

  AstStmtKind tag;
  union
  {
    AstStmtLet let;
    AstStmtReturn ret;
    AstStmtExpr expr;
  } data;
};

typedef struct Parser Parser;
struct Parser
{
  Lexer l;
  Token curr_token;
  Token peek_token;

  AstStmt *statements;
  MessageList message_list;

  Arena *arena;
};

internal void
parse_init(Parser *p, Arena *arena, Str8 input)
{
  p->arena = arena;

  lex_init(&p->l, input);
  lex_advance_token(&p->l, &p->curr_token);
  lex_advance_token(&p->l, &p->peek_token);
}

internal void parse_build_stmt_string(StrBuilder8 *b, AstStmt *statements);
internal void parse_build_expr_string(StrBuilder8 *b, AstExpr *expr);

internal void
parse_build_expr_string(StrBuilder8 *b, AstExpr *expr)
{
  if (expr == 0) return;

  switch (expr->tag)
  {
    case AstExpr_Identifier: str8_append(b, expr->data.identifier.token.value); break;
    case AstExpr_Number: str8_append(b, expr->data.number.token.value); break;
    case AstExpr_Boolean: str8_append(b, expr->data.boolean.token.value); break;

    case AstExpr_String:
      str8_append_char(b, '"');
      str8_append(b, expr->data.string.value);
      str8_append_char(b, '"');
      break;

    case AstExpr_Array:
      str8_append_lit(b, "[");
      if (expr->data.array.elements != 0)
      {
        AstExprArrayElement *element = expr->data.array.elements;
        for (;;)
        {
          parse_build_expr_string(b, element->value);
          if ((element = element->next) == 0) break;
          str8_append_lit(b, ", ");
        }
      }
      str8_append_lit(b, "]");
      break;

    case AstExpr_Prefix:
      str8_append_lit(b, "(");
      str8_append(b, expr->data.prefix.token.value);
      parse_build_expr_string(b, expr->data.prefix.rhs);
      str8_append_lit(b, ")");
      break;

    case AstExpr_Infix:
      str8_append_lit(b, "(");
      parse_build_expr_string(b, expr->data.infix.lhs);
      str8_append_lit(b, " ");
      str8_append(b, expr->data.infix.token.value);
      str8_append_lit(b, " ");
      parse_build_expr_string(b, expr->data.infix.rhs);
      str8_append_lit(b, ")");
      break;

    case AstExpr_IfElse:
      str8_append_lit(b, "if (");
      parse_build_expr_string(b, expr->data.if_else.condition);
      str8_append_lit(b, "){ ");
      parse_build_stmt_string(b, expr->data.if_else.consequence->statements);
      str8_append_lit(b, " }");
      if (expr->data.if_else.alternative != 0)
      {
        str8_append_lit(b, " else { ");
        parse_build_stmt_string(b, expr->data.if_else.alternative->statements);
        str8_append_lit(b, " }");
      }
      break;

    case AstExpr_Function:
      str8_append_lit(b, "fn(");
      if (expr->data.function.params != 0)
      {
        AstExprFunctionParam *param = expr->data.function.params;
        for (;;)
        {
          str8_append(b, param->identifier.token.value);
          if ((param = param->next) == 0) break;
          str8_append_lit(b, ", ");
        }
      }
      str8_append_lit(b, ") { ");
      parse_build_stmt_string(b, expr->data.function.body->statements);
      str8_append_lit(b, " }");
      break;

    case AstExpr_Call:
      parse_build_expr_string(b, expr->data.call.function);
      str8_append_lit(b, "(");
      if (expr->data.call.args != 0)
      {
        AstExprCallArg *arg = expr->data.call.args;
        for (;;)
        {
          parse_build_expr_string(b, arg->expr);
          if ((arg = arg->next) == 0) break;
          str8_append_lit(b, ", ");
        }
      }
      str8_append_lit(b, ")");
      break;

    case AstExpr_Index:
      str8_append_lit(b, "(");
      parse_build_expr_string(b, expr->data.index.lhs);
      str8_append_lit(b, "[");
      parse_build_expr_string(b, expr->data.index.index);
      str8_append_lit(b, "])");
      break;

    default: str8_append_lit(b, "[INVALID EXPRESSION TAG]"); break;
  }
}

internal void
parse_build_stmt_string(StrBuilder8 *b, AstStmt *statements)
{
  for (AstStmt *stmt = statements; stmt != 0; stmt = stmt->next)
  {
    switch (stmt->tag)
    {
      case AstStmt_Let:
        str8_append(b, stmt->data.let.token.value);
        str8_append_lit(b, " ");
        str8_append(b, stmt->data.let.identifier->token.value);
        str8_append_lit(b, " = ");
        parse_build_expr_string(b, stmt->data.let.expr);
        str8_append_lit(b, ";");
        break;

      case AstStmt_Ret:
        str8_append(b, stmt->data.ret.token.value);
        str8_append_lit(b, " ");
        parse_build_expr_string(b, stmt->data.ret.expr);
        str8_append_lit(b, ";");
        break;

      case AstStmt_Expr: parse_build_expr_string(b, stmt->data.expr.expr); break;

      default: str8_append_lit(b, "[INVALID STATEMENT TAG]"); break;
    }
  }
}

internal void
parse_print_messages(Parser *p)
{
  for (Message *m = p->message_list.first; m != 0; m = m->next)
  {
    printf("[%.*s] %.*s\n", str8_va(message_level_name(m->level)), str8_va(m->value));
  }
}

internal void
parse_error(Parser *p, MessageLevel level, Str8 message)
{
  Message *m = arena_alloc_t(p->arena, Message);
  m->level   = level;
  m->value   = message;

  if (p->message_list.level < level)
  {
    p->message_list.level = level;
  }

  if (!p->message_list.first)
  {
    p->message_list.first = m;
  }
  else
  {
    p->message_list.last->next = m;
  }

  p->message_list.last = m;
  p->message_list.count += 1;
}

internal void
parse_advance_token(Parser *p)
{
  p->curr_token = p->peek_token;
  lex_advance_token(&p->l, &p->peek_token);
}

internal bool
parse_expect(Parser *p, TokenKind kind)
{
  if (p->peek_token.kind == kind)
  {
    parse_advance_token(p);
    return true;
  }
  else
  {
    Str8 error = str8_f(p->arena,
                        "Expected token '%.*s' but parsed '%.*s'.",
                        str8_va(token_name(kind)),
                        str8_va(token_name(p->peek_token.kind)));
    parse_error(p, MessageLevel_Error, error);
    return false;
  }
}

internal Precedence
parse_get_precedence(TokenKind kind)
{
  switch (kind)
  {
    case TokenKind_Equal:
    case TokenKind_NotEqual: return Precedence_Equals;

    case TokenKind_Less:
    case TokenKind_Greater: return Precedence_LessGreater;

    case TokenKind_Plus:
    case TokenKind_Minus: return Precedence_Sum;

    case TokenKind_Slash:
    case TokenKind_Star: return Precedence_Product;

    case TokenKind_LParen: return Precedence_Call;

    case TokenKind_LBracket: return Precedence_Index;

    default: return Precedence_Lowest;
  }
}

internal AstExpr *parse_expr(Parser *p, Precedence precedence);
internal AstStmt *parse_stmt(Parser *p);
internal AstStmtBlock *parse_block(Parser *p);

internal AstExpr *
parse_expr_identifier(Parser *p)
{
  AstExpr *expr               = arena_alloc_t(p->arena, AstExpr);
  expr->tag                   = AstExpr_Identifier;
  expr->data.identifier.token = p->curr_token;
  return expr;
}

internal AstExpr *
parse_expr_number(Parser *p)
{
  AstExpr *expr           = arena_alloc_t(p->arena, AstExpr);
  expr->tag               = AstExpr_Number;
  expr->data.number.token = p->curr_token;

  u8 buf[64];
  usize len = min(p->curr_token.value.len, sizeof(buf) - 1);
  memcpy(buf, p->curr_token.value.buf, len);
  buf[len] = 0;

  char *end;
  errno = 0;

  s64 number_value = strtoll((char *)buf, &end, 10);

  bool is_out_of_range = errno == ERANGE;
  bool is_not_number   = end == (char *)buf;
  if (is_out_of_range || is_not_number)
  {
    Str8 error =
    is_out_of_range
    ? str8_f(p->arena, "Number literal '%.*s' is out of range.", str8_va(p->curr_token.value))
    : str8_f(p->arena, "'%.*s' is not a number literal.", str8_va(p->curr_token.value));
    parse_error(p, MessageLevel_Error, error);
  }
  else
  {
    expr->data.number.value = number_value;
  }

  return expr;
}

internal AstExpr *
parse_expr_boolean(Parser *p)
{
  AstExpr *expr            = arena_alloc_t(p->arena, AstExpr);
  expr->tag                = AstExpr_Boolean;
  expr->data.boolean.token = p->curr_token;
  expr->data.boolean.value = p->curr_token.kind == TokenKind_True;
  return expr;
}

internal const u8 EXPR_STRING_EMPTY_BUFFER[] = { 0 };

internal AstExpr *
parse_expr_string(Parser *p)
{
  AstExpr *expr           = arena_alloc_t(p->arena, AstExpr);
  expr->tag               = AstExpr_String;
  expr->data.string.token = p->curr_token;

  usize len = expr->data.string.token.value.len - 1;

  if (p->curr_token.kind == TokenKind_StringOpen)
  {
    Str8 error = str8_f(p->arena, "String literal '%.*s' is not closed.", str8_va(p->curr_token.value));
    parse_error(p, MessageLevel_Error, error);
  }
  else if (len - 1 == 0)
  {
    expr->data.string.value.buf = EXPR_STRING_EMPTY_BUFFER;
    expr->data.string.value.len = 0;
  }
  else if (p->curr_token.kind == TokenKind_StringEsc)
  {
    // TODO(tad): Consider only doing this work in the lex phase.
    TmpArena scratch = scratch_begin(p->arena);
    StrBuilder8 b    = str8_builder_create(scratch.arena, p->curr_token.value.len);

    Str8 s = p->curr_token.value;
    for (usize i = 1; i < len; i++)
    {
      if (s.buf[i] == '\\')
      {
        i += 1;
        if (i < len)
        {
          switch (s.buf[i])
          {
            case '\\': str8_append_lit(&b, "\\"); break;
            case 't': str8_append_lit(&b, "\t"); break;
            case 'n': str8_append_lit(&b, "\n"); break;

            case '"':
            // NOTE(tad): Invalid escape sequences are ignored and replaced with the "escaped" char.
            default: str8_append_char(&b, s.buf[i]); break;
          }
        }
      }
      else
      {
        str8_append_char(&b, s.buf[i]);
      }
    }

    expr->data.string.value = str8_dump(&b, p->arena);

    scratch_end(scratch);
  }
  else
  {
    // TODO(tad): Consider interning strings.
    expr->data.string.value = str8(p->curr_token.value.buf + 1, len - 1);
  }

  return expr;
}

internal AstExpr *
parse_expr_unary_op(Parser *p)
{
  Token token = p->curr_token;

  parse_advance_token(p);

  AstExpr *expr           = arena_alloc_t(p->arena, AstExpr);
  expr->tag               = AstExpr_Prefix;
  expr->data.prefix.token = token;
  expr->data.prefix.rhs   = parse_expr(p, Precedence_Prefix);
  return expr;
}

internal AstExpr *
parse_expr_if_else(Parser *p)
{
  AstExpr *expr            = arena_alloc_t(p->arena, AstExpr);
  expr->tag                = AstExpr_IfElse;
  expr->data.if_else.token = p->curr_token;

  if (!parse_expect(p, TokenKind_LParen)) return expr;
  parse_advance_token(p);
  expr->data.if_else.condition = parse_expr(p, Precedence_Lowest);
  if (!parse_expect(p, TokenKind_RParen)) return expr;

  if (!parse_expect(p, TokenKind_LBrace)) return expr;
  expr->data.if_else.consequence = parse_block(p);

  if (p->peek_token.kind == TokenKind_Else)
  {
    parse_advance_token(p);
    if (!parse_expect(p, TokenKind_LBrace)) return expr;
    expr->data.if_else.alternative = parse_block(p);
  }

  return expr;
}

internal AstExpr *
parse_expr_function(Parser *p)
{
  AstExpr *expr             = arena_alloc_t(p->arena, AstExpr);
  expr->tag                 = AstExpr_Function;
  expr->data.function.token = p->curr_token;

  if (!parse_expect(p, TokenKind_LParen)) return expr;

  bool is_valid = true;

  if (p->peek_token.kind != TokenKind_RParen)
  {
    SLL_BUILDER(AstExprFunctionParam) params;
    sll_builder_init(params);

    while (parse_expect(p, TokenKind_Identifier))
    {
      AstExprFunctionParam *param = arena_alloc_t(p->arena, AstExprFunctionParam);
      param->identifier.token     = p->curr_token;

      sll_builder_append(params, param);

      if (p->peek_token.kind == TokenKind_RParen)
      {
        parse_advance_token(p);
        break;
      }

      if (!parse_expect(p, TokenKind_Comma))
      {
        is_valid = false;
        break;
      }
    }

    if (params.count != (u8)params.count)
    {
      is_valid = false;

      Str8 error = str8_f(p->arena,
                          "Function defined with '%zu' parameters, more than max supported '255'.",
                          params.count);
      parse_error(p, MessageLevel_Error, error);
    }

    expr->data.function.params      = sll_builder_result(params);
    expr->data.function.param_count = (u8)params.count;
  }
  else
  {
    parse_advance_token(p);
  }

  if (is_valid && parse_expect(p, TokenKind_LBrace))
  {
    expr->data.function.body = parse_block(p);
  }

  return expr;
}

internal AstExpr *
parse_expr_array(Parser *p)
{
  AstExpr *array          = arena_alloc_t(p->arena, AstExpr);
  array->tag              = AstExpr_Array;
  array->data.array.token = p->curr_token;

  if (p->peek_token.kind != TokenKind_RBracket)
  {
    SLL_BUILDER(AstExprArrayElement) elements;
    sll_builder_init(elements);

    for (;;)
    {
      parse_advance_token(p);

      AstExprArrayElement *element = arena_alloc_t(p->arena, AstExprArrayElement);
      element->value               = parse_expr(p, Precedence_Lowest);

      sll_builder_append(elements, element);

      if (p->peek_token.kind == TokenKind_RBracket)
      {
        parse_advance_token(p);
        break;
      }

      if (!parse_expect(p, TokenKind_Comma))
      {
        break;
      }
    }

    array->data.array.elements = sll_builder_result(elements);
    array->data.array.count    = elements.count;
  }
  else
  {
    parse_advance_token(p);
  }

  return array;
}

internal AstExpr *
parse_expr_grouped(Parser *p)
{
  parse_advance_token(p);

  AstExpr *grouped_expr = parse_expr(p, Precedence_Lowest);
  parse_expect(p, TokenKind_RParen);
  return grouped_expr;
}

internal AstExpr *
parse_expr_prefix(Parser *p)
{
  switch (p->curr_token.kind)
  {
    case TokenKind_Identifier: return parse_expr_identifier(p);
    case TokenKind_Number: return parse_expr_number(p);
    case TokenKind_If: return parse_expr_if_else(p);
    case TokenKind_Function: return parse_expr_function(p);
    case TokenKind_LBracket: return parse_expr_array(p);
    case TokenKind_LParen: return parse_expr_grouped(p);

    case TokenKind_True:
    case TokenKind_False: return parse_expr_boolean(p);

    case TokenKind_String:
    case TokenKind_StringEsc: return parse_expr_string(p);

    case TokenKind_Minus:
    case TokenKind_Bang: return parse_expr_unary_op(p);

    default:
    {
      Str8 error = str8_f(p->arena,
                          "'%.*s' (type '%.*s') is not a valid prefix token.",
                          str8_va(p->curr_token.value),
                          str8_va(token_name(p->curr_token.kind)));
      parse_error(p, MessageLevel_Error, error);
      return 0;
    }
  }
}

internal AstExpr *
parse_expr_binary_op(Parser *p, AstExpr *lhs)
{
  parse_advance_token(p);

  AstExpr *expr          = arena_alloc_t(p->arena, AstExpr);
  expr->tag              = AstExpr_Infix;
  expr->data.infix.token = p->curr_token;
  expr->data.infix.lhs   = lhs;

  // NOTE(tad): Decreasing the infix precedence here is one way to support
  // right-associative operators.
  Precedence infix_precedence = parse_get_precedence(p->curr_token.kind);
  parse_advance_token(p);
  expr->data.infix.rhs = parse_expr(p, infix_precedence);

  return expr;
}

internal AstExpr *
parse_expr_call(Parser *p, AstExpr *lhs)
{
  parse_advance_token(p);

  AstExpr *call            = arena_alloc_t(p->arena, AstExpr);
  call->tag                = AstExpr_Call;
  call->data.call.token    = p->curr_token;
  call->data.call.function = lhs;

  if (p->peek_token.kind != TokenKind_RParen)
  {
    SLL_BUILDER(AstExprCallArg) args;
    sll_builder_init(args);

    for (;;)
    {
      parse_advance_token(p);

      AstExprCallArg *arg = arena_alloc_t(p->arena, AstExprCallArg);
      arg->expr           = parse_expr(p, Precedence_Lowest);

      sll_builder_append(args, arg);

      if (p->peek_token.kind == TokenKind_RParen)
      {
        parse_advance_token(p);
        break;
      }

      if (!parse_expect(p, TokenKind_Comma))
      {
        break;
      }
    }

    if (args.count != (u8)args.count)
    {
      Str8 error = str8_f(p->arena,
                          "Function call provided '%zu' arguments, more than max supported '255'.",
                          args.count);
      parse_error(p, MessageLevel_Error, error);
    }

    call->data.call.args      = sll_builder_result(args);
    call->data.call.arg_count = (u8)args.count;
  }
  else
  {
    parse_advance_token(p);
  }

  return call;
}

internal AstExpr *
parse_expr_index(Parser *p, AstExpr *lhs)
{
  parse_advance_token(p);

  AstExpr *expr          = arena_alloc_t(p->arena, AstExpr);
  expr->tag              = AstExpr_Index;
  expr->data.index.token = p->curr_token;
  expr->data.index.lhs   = lhs;

  parse_advance_token(p);
  expr->data.index.index = parse_expr(p, Precedence_Lowest);

  parse_expect(p, TokenKind_RBracket);

  return expr;
}

internal AstExpr *
parse_expr_infix(Parser *p, AstExpr *lhs)
{
  switch (p->peek_token.kind)
  {
    case TokenKind_Plus:
    case TokenKind_Minus:
    case TokenKind_Slash:
    case TokenKind_Star:
    case TokenKind_Equal:
    case TokenKind_NotEqual:
    case TokenKind_Less:
    case TokenKind_Greater: return parse_expr_binary_op(p, lhs);
    case TokenKind_LParen: return parse_expr_call(p, lhs);
    case TokenKind_LBracket: return parse_expr_index(p, lhs);

    default: return lhs;
  }
}

internal AstExpr *
parse_expr(Parser *p, Precedence precedence)
{
  AstExpr *lhs = parse_expr_prefix(p);

  while (p->curr_token.kind != TokenKind_Semicolon &&
         precedence < parse_get_precedence(p->peek_token.kind))
  {
    lhs = parse_expr_infix(p, lhs);
  }

  return lhs;
}

internal AstStmt *
parse_stmt(Parser *p)
{
  AstStmt *stmt = 0;

  switch (p->curr_token.kind)
  {
    case TokenKind_Let:
    {
      Token let_token   = p->curr_token;
      Token ident_token = p->peek_token;
      if (!parse_expect(p, TokenKind_Identifier)) break;
      if (!parse_expect(p, TokenKind_Assign)) break;

      parse_advance_token(p);

      stmt                             = arena_alloc_t(p->arena, AstStmt);
      stmt->tag                        = AstStmt_Let;
      stmt->data.let.token             = let_token;
      stmt->data.let.identifier        = arena_alloc_t(p->arena, AstExprIdentifier);
      stmt->data.let.identifier->token = ident_token;
      stmt->data.let.expr              = parse_expr(p, Precedence_Lowest);

      if (p->peek_token.kind == TokenKind_Semicolon)
      {
        parse_advance_token(p);
      }

      break;
    }

    case TokenKind_Return:
    {
      Token ret_token = p->curr_token;

      parse_advance_token(p);

      stmt                 = arena_alloc_t(p->arena, AstStmt);
      stmt->tag            = AstStmt_Ret;
      stmt->data.ret.token = ret_token;
      stmt->data.ret.expr  = parse_expr(p, Precedence_Lowest);

      if (p->peek_token.kind == TokenKind_Semicolon)
      {
        parse_advance_token(p);
      }

      break;
    }

    case TokenKind_Semicolon: break;

    case TokenKind_Illegal:
    {
      for (usize i = 0; i < p->curr_token.value.len; i++)
      {
        u8 c       = p->curr_token.value.buf[i];
        Str8 error = c >= 32 && c <= 126 ? str8_f(p->arena, "Illegal token: %c", c)
                                         : str8_f(p->arena, "Illegal unprintable token: \\x%X", c);
        parse_error(p, MessageLevel_Error, error);
      }
      break;
    }

    case TokenKind_EOF: assert(0 && "EOF tokens are checked by outer loop condition."); break;

    default:
    {
      Token expr_token = p->curr_token;

      stmt                  = arena_alloc_t(p->arena, AstStmt);
      stmt->tag             = AstStmt_Expr;
      stmt->data.expr.token = expr_token;
      stmt->data.expr.expr  = parse_expr(p, Precedence_Lowest);

      if (p->peek_token.kind == TokenKind_Semicolon)
      {
        parse_advance_token(p);
      }

      break;
    }
  }

  return stmt;
}

internal AstStmtBlock *
parse_block(Parser *p)
{
  AstStmtBlock *block = arena_alloc_t(p->arena, AstStmtBlock);
  block->token        = p->curr_token;

  parse_advance_token(p);

  SLL_BUILDER(AstStmt) stmts;
  sll_builder_init(stmts);

  while (p->curr_token.kind != TokenKind_RBrace && p->curr_token.kind != TokenKind_EOF)
  {
    AstStmt *stmt = parse_stmt(p);
    if (stmt)
    {
      sll_builder_append(stmts, stmt);
    }

    parse_advance_token(p);
  }

  block->statements = sll_builder_result(stmts);

  if (p->curr_token.kind == TokenKind_EOF)
  {
    parse_error(p, MessageLevel_Error, str8_lit("Block reached EOF before closing '}'."));
  }

  return block;
}

internal Parser
parse(Arena *arena, Str8 input)
{
  Parser p = { 0 };
  parse_init(&p, arena, input);

  SLL_BUILDER(AstStmt) stmts;
  sll_builder_init(stmts);

  while (p.curr_token.kind != TokenKind_EOF)
  {
    AstStmt *stmt = parse_stmt(&p);
    if (stmt)
    {
      sll_builder_append(stmts, stmt);
    }

    parse_advance_token(&p);
  }

  p.statements = sll_builder_result(stmts);

  return p;
}

////////////////////////////////////////////////////////////////////////////////
// eval

typedef struct Object Object;

#define ENV_LOAD_FACTOR 0.75

typedef struct EnvVar EnvVar;
struct EnvVar
{
  Str8 name;
  Object const *value;
};

typedef struct Env Env;
struct Env
{
  Env *outer;
  Arena *arena;

  u32 cnt;
  u32 cap;
  EnvVar *vars;
};

internal bool
env_is_empty(EnvVar var)
{
  return var.name.buf == 0;
}

internal u32
env_hash(Str8 key)
{
  u32 hash = 2166136261;
  for (usize i = 0; i < key.len; i++)
  {
    hash ^= (u32)key.buf[i];
    hash *= 16777619;
  }
  return hash;
}

internal void
env_init(Env *env, Arena *arena, u32 initial_capacity)
{
  u32 cap = max(initial_capacity, 16);

  env->arena = arena;
  env->outer = 0;
  env->cnt   = 0;
  env->cap   = cap;
  env->vars  = arena_alloc_tn(arena, EnvVar, cap);
}

#define env_init_count(env, arena, initial_count) \
  env_init((env), (arena), ceil_pos(u32, (initial_count) / ENV_LOAD_FACTOR));

internal EnvVar *
env_find(Env const *env, Str8 name)
{
  u32 cap = env->cap;
  u32 h   = env_hash(name) % cap;

  for (;;)
  {
    EnvVar *var = &env->vars[h];

    if (env_is_empty(*var) || str8_equal(var->name, name))
    {
      return var;
    }

    h = (h + 1) % cap;
  }
}

internal void
env_grow(Env *env)
{
  TmpArena scratch = scratch_begin(env->arena);

  u32 old_cap = env->cap;
  u32 new_cap = old_cap * 2;

  EnvVar *old_vars = arena_alloc_tn_nz(scratch.arena, EnvVar, old_cap);
  memcpy(old_vars, env->vars, old_cap * sizeof(*env->vars));

  EnvVar *new_vars = arena_realloc_t_nz(env->arena, env->vars, EnvVar, env->cap, new_cap);
  memset(new_vars, 0, new_cap * sizeof(*env->vars));

  env->vars = new_vars;
  env->cap  = new_cap;

  for (usize i = 0; i < old_cap; i++)
  {
    EnvVar old_var = old_vars[i];
    if (!env_is_empty(old_var))
    {
      EnvVar *var = env_find(env, old_var.name);
      *var        = old_var;
    }
  }

  scratch_end(scratch);
}

internal Object const *
env_get(Env const *env, Str8 name)
{
  Object const *value;
  do
  {
    value = env_find(env, name)->value;
    if (value != 0) break;
    env = env->outer;
  } while (env != 0);
  return value;
}

internal void
env_set(Env *env, Str8 name, Object const *value)
{
  if (env->cnt + 1 > env->cap * ENV_LOAD_FACTOR)
  {
    env_grow(env);
  }

  EnvVar *var = env_find(env, name);
  if (env_is_empty(*var))
  {
    env->cnt += 1;
  }

  var->name  = name;
  var->value = value;
}

internal Env *env_builtin = &(Env){ 0 };

#define OBJECT_KINDS(X) \
  X(Number)             \
  X(Boolean)            \
  X(String)             \
  X(Array)              \
  X(Return)             \
  X(Function)           \
  X(Builtin)            \
  X(Null)               \
  X(Error)

typedef enum
{
#define OBJECT_KIND(name) ObjectKind_##name,
  OBJECT_KINDS(OBJECT_KIND)
#undef OBJECT_KIND
} ObjectKind;

internal Str8
object_name(ObjectKind kind)
{
  switch (kind)
  {
#define OBJECT_KIND(name) \
  case ObjectKind_##name: return str8_lit(#name);
    OBJECT_KINDS(OBJECT_KIND)
#undef OBJECT_KIND
    default: return str8_lit("Unknown Object Kind");
  }
}

typedef struct ObjectNumber ObjectNumber;
struct ObjectNumber
{
  s64 value;
};

typedef struct ObjectBoolean ObjectBoolean;
struct ObjectBoolean
{
  b8 value;
};

typedef struct ObjectString ObjectString;
struct ObjectString
{
  Str8 value;
};

typedef struct ObjectArray ObjectArray;
struct ObjectArray
{
  Object const **elements;
  usize len;
  usize cap;
};

typedef struct ObjectNull ObjectNull;
struct ObjectNull
{
  u8 unused_;
};

typedef struct ObjectReturn ObjectReturn;
struct ObjectReturn
{
  Object const *value;
};

typedef struct ObjectFunction ObjectFunction;
struct ObjectFunction
{
  AstExprFunctionParam *params;
  AstStmtBlock *body;
  Env *env;
};

typedef struct ObjectBuiltin ObjectBuiltin;
struct ObjectBuiltin
{
  Str8 name;
  Object const *(*fn)(Arena *, Object const **, u8); // fn(arena, args, arg_count)
};

typedef struct ObjectError ObjectError;
struct ObjectError
{
  Str8 value;
};

struct Object
{
  ObjectKind tag;
  union
  {
    ObjectNumber number;
    ObjectBoolean boolean;
    ObjectString string;
    ObjectArray array;
    ObjectReturn ret;
    ObjectFunction function;
    ObjectBuiltin builtin;
    ObjectNull null;
    ObjectError err;
  } data;
};

internal Object const *OBJECT_TRUE  = &(Object){ ObjectKind_Boolean, { .boolean.value = true } };
internal Object const *OBJECT_FALSE = &(Object){ ObjectKind_Boolean, { .boolean.value = false } };
internal Object const *OBJECT_NULL  = &(Object){ ObjectKind_Null, { 0 } };

internal Str8
eval_inspect(Arena *arena, Object const *o)
{
  switch (o->tag)
  {
    case ObjectKind_Number: return str8_f(arena, "%lld", o->data.number.value);
    case ObjectKind_Boolean: return o->data.boolean.value ? KEYWORD_TRUE : KEYWORD_FALSE;
    case ObjectKind_String: return str8_f(arena, "\"%.*s\"", str8_va(o->data.string.value));
    case ObjectKind_Return: return eval_inspect(arena, o->data.ret.value);
    case ObjectKind_Array:
    {
      StrBuilder8 b = str8_builder_create_default(arena);
      str8_append_lit(&b, "[");
      if (o->data.array.len != 0)
      {
        usize i;
        for (i = 0; i < o->data.array.len - 1; i++)
        {
          str8_append(&b, eval_inspect(arena, o->data.array.elements[i]));
          str8_append_lit(&b, ", ");
        }
        str8_append(&b, eval_inspect(arena, o->data.array.elements[i]));
      }
      str8_append_lit(&b, "]");

      return str8_build(&b);
    }
    case ObjectKind_Function:
    {
      StrBuilder8 b = str8_builder_create_default(arena);
      str8_append_lit(&b, "fn(");
      if (o->data.function.params != 0)
      {
        AstExprFunctionParam *param = o->data.function.params;
        for (;;)
        {
          str8_append(&b, param->identifier.token.value);
          if ((param = param->next) == 0) break;
          str8_append_lit(&b, ", ");
        }
      }
      str8_append_lit(&b, ") { ");
      parse_build_stmt_string(&b, o->data.function.body->statements);
      str8_append_lit(&b, " }");

      return str8_build(&b);
    }
    case ObjectKind_Builtin:
    {
      StrBuilder8 b = str8_builder_create_default(arena);
      str8_append_lit(&b, "builtin:");
      str8_append(&b, o->data.builtin.name);
      return str8_build(&b);
    }
    case ObjectKind_Null: return str8_lit("");
    case ObjectKind_Error: return str8_f(arena, "Error: %.*s", str8_va(o->data.err.value));
    default:
      return str8_f(arena, "unsupported object inspection: %.*s", str8_va(object_name(o->tag)));
  }
}

internal Object const *eval_program(Arena *arena, AstStmt *statements, Env *env);
internal Object const *eval_block(Arena *arena, AstStmtBlock *block, Env *env);
internal Object const *eval_expr(Arena *arena, AstExpr *expr, Env *env);
internal Object const *eval_expr_prefix(Arena *arena, AstExprPrefix prefix, Env *env);
internal Object const *eval_expr_infix(Arena *arena, AstExprInfix infix, Env *env);
internal Object const *eval_expr_if_else(Arena *arena, AstExprIfElse if_else, Env *env);
internal Object const *eval_expr_call(Arena *arena, AstExprCall call, Env *env);
internal Object const *eval_expr_array(Arena *arena, AstExprArray array, Env *env);

internal Object const *
object_from_number(Arena *arena, s64 value)
{
  Object *result            = arena_alloc_t(arena, Object);
  result->tag               = ObjectKind_Number;
  result->data.number.value = value;
  return result;
}

internal Object const *
object_from_bool(bool value)
{
  return value ? OBJECT_TRUE : OBJECT_FALSE;
}

internal Object const *
object_from_string(Arena *arena, Str8 value)
{
  Object *result            = arena_alloc_t(arena, Object);
  result->tag               = ObjectKind_String;
  result->data.string.value = value;
  return result;
}

internal Object const *
object_from_array(Arena *arena, Object const **elements, usize len, usize cap)
{
  Object *result              = arena_alloc_t(arena, Object);
  result->tag                 = ObjectKind_Array;
  result->data.array.elements = elements;
  result->data.array.len      = len;
  result->data.array.cap      = cap;
  return result;
}

internal bool
object_is_truthy(Object const *object)
{
  return object->tag != ObjectKind_Null &&
         (object->tag != ObjectKind_Boolean || object->data.boolean.value);
}

internal Object const *
object_from_function(Arena *arena, AstExprFunction fn, Env *env)
{
  Object *result               = arena_alloc_t(arena, Object);
  result->tag                  = ObjectKind_Function;
  result->data.function.params = fn.params;
  result->data.function.body   = fn.body;
  result->data.function.env    = env;
  return result;
}

internal Object const *
object_from_error(Arena *arena, Str8 error)
{
  Object *result         = arena_alloc_t(arena, Object);
  result->tag            = ObjectKind_Error;
  result->data.err.value = error;
  return result;
}

internal bool
object_is_error(Object const *object)
{
  return object->tag == ObjectKind_Error;
}

internal Object const *
eval_expr_prefix(Arena *arena, AstExprPrefix prefix, Env *env)
{
  Object const *result;

  switch (prefix.token.kind)
  {
    case TokenKind_Bang:
    {
      Object const *rhs = eval_expr(arena, prefix.rhs, env);
      if (object_is_error(rhs))
      {
        result = object_from_error(arena, rhs->data.err.value);
      }
      else
      {
        result = object_from_bool(!object_is_truthy(rhs));
      }
      break;
    }

    case TokenKind_Minus:
    {
      Object const *rhs = eval_expr(arena, prefix.rhs, env);
      if (object_is_error(rhs))
      {
        result = object_from_error(arena, rhs->data.err.value);
      }
      else if (rhs->tag != ObjectKind_Number)
      {
        Str8 error = str8_f(arena, "unknown operator: -%.*s", str8_va(object_name(rhs->tag)));
        result     = object_from_error(arena, error);
      }
      else
      {
        result = object_from_number(arena, -rhs->data.number.value);
      }
      break;
    }

    default:
    {
      Str8 error = str8_f(arena,
                          "unknown operator: %.*s%.*s",
                          str8_va(prefix.token.value),
                          str8_va(ast_expr_name(prefix.rhs->tag)));
      result     = object_from_error(arena, error);
    }
  }

  return result;
}

internal Object const *
object_from_unknown_infix_op(Arena *arena, AstExprInfix infix, Object const *lhs, Object const *rhs)
{
  Str8 error = str8_f(arena,
                      "unknown operator: %.*s %.*s %.*s",
                      str8_va(object_name(lhs->tag)),
                      str8_va(infix.token.value),
                      str8_va(object_name(rhs->tag)));
  return object_from_error(arena, error);
}

internal Object const *
eval_expr_infix(Arena *arena, AstExprInfix infix, Env *env)
{
  Object const *result;

  Object const *lhs = eval_expr(arena, infix.lhs, env);
  Object const *rhs = eval_expr(arena, infix.rhs, env);
  TokenKind op      = infix.token.kind;

  if (object_is_error(lhs))
  {
    result = object_from_error(arena, lhs->data.err.value);
  }
  else if (object_is_error(rhs))
  {
    result = object_from_error(arena, rhs->data.err.value);
  }
  else if (lhs->tag == ObjectKind_Number && rhs->tag == ObjectKind_Number)
  {
    switch (op)
    {
      case TokenKind_Plus:
        result = object_from_number(arena, lhs->data.number.value + rhs->data.number.value);
        break;
      case TokenKind_Minus:
        result = object_from_number(arena, lhs->data.number.value - rhs->data.number.value);
        break;
      case TokenKind_Star:
        result = object_from_number(arena, lhs->data.number.value * rhs->data.number.value);
        break;
      case TokenKind_Slash:
        result = object_from_number(arena, lhs->data.number.value / rhs->data.number.value);
        break;
      case TokenKind_Less:
        result = object_from_bool(lhs->data.number.value < rhs->data.number.value);
        break;
      case TokenKind_Greater:
        result = object_from_bool(lhs->data.number.value > rhs->data.number.value);
        break;
      case TokenKind_Equal:
        result = object_from_bool(lhs->data.number.value == rhs->data.number.value);
        break;
      case TokenKind_NotEqual:
        result = object_from_bool(lhs->data.number.value != rhs->data.number.value);
        break;
      default: result = object_from_unknown_infix_op(arena, infix, lhs, rhs); break;
    }
  }
  else if (lhs->tag == ObjectKind_String && rhs->tag == ObjectKind_String)
  {
    switch (op)
    {
      case TokenKind_Plus:
      {
        Str8 value = str8_concat(arena, lhs->data.string.value, rhs->data.string.value);
        result     = object_from_string(arena, value);
        break;
      }
      case TokenKind_Equal:
        result = object_from_bool(str8_equal(lhs->data.string.value, rhs->data.string.value));
        break;
      case TokenKind_NotEqual:
        result = object_from_bool(!str8_equal(lhs->data.string.value, rhs->data.string.value));
        break;
      default: result = object_from_unknown_infix_op(arena, infix, lhs, rhs); break;
    }
  }
  else
  {
    switch (op)
    {
      // NOTE(tad): Non-number equality is determined by reference comparisons.
      // Boolean and null objects should always reference singletons.
      case TokenKind_Equal: result = object_from_bool(lhs == rhs); break;
      case TokenKind_NotEqual: result = object_from_bool(lhs != rhs); break;
      default:
      {
        if (lhs->tag == rhs->tag)
        {
          result = object_from_unknown_infix_op(arena, infix, lhs, rhs);
        }
        else
        {
          Str8 error = str8_f(arena,
                              "type mismatch: %.*s %.*s %.*s",
                              str8_va(object_name(lhs->tag)),
                              str8_va(infix.token.value),
                              str8_va(object_name(rhs->tag)));
          result     = object_from_error(arena, error);
        }
        break;
      }
    }
  }

  return result;
}

internal Object const *
eval_expr_if_else(Arena *arena, AstExprIfElse if_else, Env *env)
{
  Object const *condition = eval_expr(arena, if_else.condition, env);

  if (object_is_error(condition))
  {
    return condition;
  }
  if (object_is_truthy(condition))
  {
    return eval_block(arena, if_else.consequence, env);
  }
  if (if_else.alternative != 0)
  {
    return eval_block(arena, if_else.alternative, env);
  }

  return OBJECT_NULL;
}

internal Object const *
eval_call_function(Arena *arena, AstExprCall call, ObjectFunction call_fn, Env *env)
{
  // NOTE(tad): These arenas are currently the same, and neither of them are
  // scratch arenas. Still passing them both in the off chance a scratch arena
  // is later passed in for one of them. If both are different scratch arenas
  // will fail unless the number of scratch arenas is increased beyond 2.
  TmpArena scratch = scratch_begin(arena, env->arena, call_fn.env->arena);

  struct CallArgBinding
  {
    Str8 name;
    Object const *value;
  };

  struct CallArgBinding *args = arena_alloc_tn(scratch.arena, struct CallArgBinding, call.arg_count);

  u8 arg_index                     = 0;
  AstExprCallArg *arg_expr         = call.args;
  AstExprFunctionParam *param_expr = call_fn.params;
  while (arg_expr != 0 && param_expr != 0 && arg_index < call.arg_count)
  {
    Object const *arg_value = eval_expr(arena, arg_expr->expr, env);
    if (object_is_error(arg_value)) return arg_value;

    args[arg_index].name  = param_expr->identifier.token.value;
    args[arg_index].value = arg_value;

    arg_index += 1;
    arg_expr   = arg_expr->next;
    param_expr = param_expr->next;
  }

  if (param_expr != 0)
  {
    Str8 error =
    str8_f(arena, "missing function parameter: %.*s", str8_va(param_expr->identifier.token.value));
    return object_from_error(arena, error);
  }

  Env *extended_env = arena_alloc_t(arena, Env);
  env_init_count(extended_env, arena, call.arg_count);
  extended_env->outer = call_fn.env;

  for (u8 i = 0; i < arg_index; i++)
  {
    env_set(extended_env, args[i].name, args[i].value);
  }

  scratch_end(scratch);

  Object const *result = eval_block(arena, call_fn.body, extended_env);
  return result->tag == ObjectKind_Return ? result->data.ret.value : result;
}

internal Object const *
eval_call_builtin(Arena *arena, AstExprCall call, ObjectBuiltin call_builtin, Env *env)
{
  // NOTE(tad): These arenas are currently the same, and neither of them are
  // scratch arenas. Still passing them both in the off chance a scratch arena
  // is later passed in for one of them. If both are different scratch arenas
  // will fail unless the number of scratch arenas is increased beyond 2.
  TmpArena scratch = scratch_begin(arena, env->arena);

  Object const **args = arena_alloc_tn(scratch.arena, Object const *, call.arg_count);

  AstExprCallArg *arg = call.args;
  for (u8 i = 0; arg != 0 && i < call.arg_count; i++)
  {
    Object const *arg_value = eval_expr(arena, arg->expr, env);
    if (object_is_error(arg_value)) return arg_value;

    args[i] = arg_value;
    arg     = arg->next;
  }

  Object const *result = call_builtin.fn(arena, args, call.arg_count);

  scratch_end(scratch);

  return result;
}

internal Object const *
eval_expr_call(Arena *arena, AstExprCall call, Env *env)
{
  Object const *call_target = eval_expr(arena, call.function, env);
  if (object_is_error(call_target)) return call_target;

  if (call_target->tag == ObjectKind_Function)
  {
    return eval_call_function(arena, call, call_target->data.function, env);
  }
  else if (call_target->tag == ObjectKind_Builtin)
  {
    return eval_call_builtin(arena, call, call_target->data.builtin, env);
  }
  else
  {
    Str8 error = str8_f(arena, "not a function: %.*s", str8_va(object_name(call_target->tag)));
    return object_from_error(arena, error);
  }
}

internal Object const *
eval_expr_array(Arena *arena, AstExprArray array, Env *env)
{
  usize len = array.count;
  usize cap = max(next_pow2(len), 4);

  Object const **elements = arena_alloc_tn(arena, Object const *, cap);

  AstExprArrayElement *element_expr = array.elements;
  for (u8 i = 0; element_expr != 0 && i < array.count; i++)
  {
    Object const *element = eval_expr(arena, element_expr->value, env);
    if (object_is_error(element)) return element;

    elements[i]  = element;
    element_expr = element_expr->next;
  }

  return object_from_array(arena, elements, len, cap);
}

internal Object const *
eval_index_array(ObjectArray array, ObjectNumber index)
{
  return index.value >= 0 && (usize)index.value < array.len ? array.elements[index.value] : OBJECT_NULL;
}

internal Object const *
eval_expr_index(Arena *arena, AstExprIndex index_expr, Env *env)
{
  Object const *target = eval_expr(arena, index_expr.lhs, env);
  if (object_is_error(target)) return target;

  Object const *index = eval_expr(arena, index_expr.index, env);
  if (object_is_error(index)) return index;

  switch (target->tag)
  {
    case ObjectKind_Array:
    {
      if (index->tag != ObjectKind_Number)
      {
        Str8 error = str8_f(arena, "invalid array index type: %.*s", str8_va(object_name(index->tag)));
        return object_from_error(arena, error);
      }

      return eval_index_array(target->data.array, index->data.number);
    }

    default:
    {
      Str8 error =
      str8_f(arena, "indexing not supported for type: %.*s", str8_va(object_name(target->tag)));
      return object_from_error(arena, error);
    }
  }
}

internal Object const *
eval_expr(Arena *arena, AstExpr *expr, Env *env)
{
  switch (expr->tag)
  {
    case AstExpr_Identifier:
    {
      Str8 name           = expr->data.identifier.token.value;
      Object const *value = env_get(env, name);
      if (value == 0)
      {
        value = env_get(env_builtin, name);
        if (value == 0)
        {
          Str8 error = str8_f(arena, "identifier not found: %.*s", str8_va(name));
          value      = object_from_error(arena, error);
        }
      }
      return value;
    }
    case AstExpr_Number: return object_from_number(arena, expr->data.number.value);
    case AstExpr_Boolean: return object_from_bool(expr->data.boolean.value);
    case AstExpr_String: return object_from_string(arena, expr->data.string.value);
    case AstExpr_Function: return object_from_function(arena, expr->data.function, env);

    case AstExpr_Prefix: return eval_expr_prefix(arena, expr->data.prefix, env);
    case AstExpr_Infix: return eval_expr_infix(arena, expr->data.infix, env);
    case AstExpr_IfElse: return eval_expr_if_else(arena, expr->data.if_else, env);
    case AstExpr_Call: return eval_expr_call(arena, expr->data.call, env);
    case AstExpr_Array: return eval_expr_array(arena, expr->data.array, env);
    case AstExpr_Index: return eval_expr_index(arena, expr->data.index, env);

    default: return OBJECT_NULL;
  }
}

internal Object const *
eval_stmt(Arena *arena, AstStmt *stmt, Env *env)
{
  switch (stmt->tag)
  {
    case AstStmt_Let:
    {
      Object const *value = eval_expr(arena, stmt->data.let.expr, env);
      if (object_is_error(value)) return value;

      env_set(env, stmt->data.let.identifier->token.value, value);

      return OBJECT_NULL;
    }

    case AstStmt_Ret:
    {
      Object const *ret_value = eval_expr(arena, stmt->data.ret.expr, env);
      if (object_is_error(ret_value)) return ret_value;

      Object *ret         = arena_alloc_t(arena, Object);
      ret->tag            = ObjectKind_Return;
      ret->data.ret.value = ret_value;
      return ret;
    }

    case AstStmt_Expr: return eval_expr(arena, stmt->data.expr.expr, env);

    default: return OBJECT_NULL;
  }
}

internal Object const *
eval_block(Arena *arena, AstStmtBlock *block, Env *env)
{
  Object const *result = OBJECT_NULL;

  for (AstStmt *stmt = block->statements; stmt != 0; stmt = stmt->next)
  {
    result = eval_stmt(arena, stmt, env);

    if (result->tag == ObjectKind_Return) return result;
    if (result->tag == ObjectKind_Error) return result;
  }

  return result;
}

// TODO(tad): This doesn't do any sort of memory management yet. While some intermediate
// evaluations are freed after use (and this might actually introduce bugs), any objects
// saved to an environment and environments themselves live for the life of the program
// (or until the provided arena is freed).
//
// Consider a simple GC implementation. Maybe a free list built on top of arenas?
internal Object const *
eval_program(Arena *arena, AstStmt *statements, Env *env)
{
  Object const *result = OBJECT_NULL;

  for (AstStmt *stmt = statements; stmt != 0; stmt = stmt->next)
  {
    result = eval_stmt(arena, stmt, env);

    if (result->tag == ObjectKind_Return) return result->data.ret.value;
    if (result->tag == ObjectKind_Error) return result;
  }

  return result;
}

internal Object const *
eval_builtin_len(Arena *arena, Object const **args, u8 arg_count)
{
  if (arg_count != 1)
  {
    Str8 error = str8_f(arena, "builtin 'len' requires 1 argument, received: %u", arg_count);
    return object_from_error(arena, error);
  }

  Object const *arg = *args;
  if (arg->tag != ObjectKind_String)
  {
    Str8 error =
    str8_f(arena, "argument to builtin 'len' not supported: %.*s", str8_va(object_name(arg->tag)));
    return object_from_error(arena, error);
  }

  return object_from_number(arena, (s64)arg->data.string.value.len);
}

internal Object const *
eval_builtin_puts(Arena *arena, Object const **args, u8 arg_count)
{
  if (arg_count > 0)
  {
    u8 i = 0;
    for (;;)
    {
      TmpArena scratch = scratch_begin(arena);
      Str8 output      = eval_inspect(scratch.arena, args[i]);
      if (args[i]->tag == ObjectKind_String)
      {
        output = str8_slice(output, 1, output.len - 1);
      }
      else if (args[i]->tag == ObjectKind_Null)
      {
        output = str8_lit("null");
      }
      printf("%.*s", str8_va(output));
      scratch_end(scratch);

      if (++i == arg_count) break;
      printf(" ");
    }
  }
  printf("\n");

  return OBJECT_NULL;
}

internal Object const *builtin_len = &(Object){ .tag               = ObjectKind_Builtin,
                                                .data.builtin.name = str8_lit("len"),
                                                .data.builtin.fn   = eval_builtin_len };

internal Object const *builtin_puts = &(Object){ .tag               = ObjectKind_Builtin,
                                                 .data.builtin.name = str8_lit("puts"),
                                                 .data.builtin.fn   = eval_builtin_puts };

internal void
env_builtin_init(Arena *arena)
{
  env_init(env_builtin, arena, 0);
  env_set(env_builtin, builtin_len->data.builtin.name, builtin_len);
  env_set(env_builtin, builtin_puts->data.builtin.name, builtin_puts);
}

////////////////////////////////////////////////////////////////////////////////
// main

internal int test(void);
internal int rlpl(void);
internal int rppl(void);
internal int repl(void);

int
main(int argc, char *argv[])
{
  if (argc > 1)
  {
    if (strcmp(argv[1], "test") == 0)
    {
      // TODO(tad): Remove tests from main program.
      return test();
    }

    if (strcmp(argv[1], "lex") == 0)
    {
      return rlpl();
    }

    if (strcmp(argv[1], "parse") == 0)
    {
      return rppl();
    }

    /*
    FILE *f = fopen(argv[1], "rb");
    {
      fseek(f, 0, SEEK_END);
      s64 size = ftell(f);
      fseek(f, 0, SEEK_SET);
      input.len = size;
    }
    u8 *buf = (u8 *)malloc(sizeof(*input.buf) * input.len);
    fread(buf, input.len, 1, f);
    input.buf = buf;
    fclose(f);
    */
  }

  return repl();
}

////////////////////////////////////////////////////////////////////////////////
// rlpl

internal int
rlpl(void)
{
  char const *PROMPT = ">> ";
  char buffer[KiB(4)];
  bool print_prompt = true;

  for (;;)
  {
    if (print_prompt) printf("%s", PROMPT);

    if (fgets(buffer, sizeof(buffer), stdin) == NULL)
    {
      if (feof(stdin))
      {
        return 0;
      }
      else
      {
        perror("read input failed");
        clearerr(stdin);
        continue;
      }
    }

    Str8 input = str8_from_cstr(buffer);
    lex_print(input);

    print_prompt = input.buf[input.len - 1] == '\n';
  }
}

////////////////////////////////////////////////////////////////////////////////
// rppl

internal int
rppl(void)
{
  char const *PROMPT = ">> ";
  char buffer[KiB(4)];
  bool print_prompt = true;

  for (;;)
  {
    if (print_prompt) printf("%s", PROMPT);

    if (fgets(buffer, sizeof(buffer), stdin) == NULL)
    {
      if (feof(stdin))
      {
        return 0;
      }
      else
      {
        perror("read input failed");
        clearerr(stdin);
        continue;
      }
    }

    TmpArena scratch = scratch_begin(0);
    Str8 input       = str8_from_cstr(buffer);
    Parser p         = parse(scratch.arena, input);

    parse_print_messages(&p);

    if (p.message_list.level < MessageLevel_Error)
    {
      StrBuilder8 b = str8_builder_create_default(scratch.arena);
      parse_build_stmt_string(&b, p.statements);
      printf("%.*s\n", str8_va(str8_build(&b)));
    }

    scratch_end(scratch);

    print_prompt = input.buf[input.len - 1] == '\n';
  }
}

////////////////////////////////////////////////////////////////////////////////
// repl

internal int
repl(void)
{
  char const *PROMPT = ">> ";
  char buffer[KiB(4)];
  bool print_prompt = true;

  Arena *arena = arena_create(MiB(8));

  env_builtin_init(arena);

  Env env = { 0 };
  env_init(&env, arena, 1024);

  for (;;)
  {
    if (print_prompt) printf("%s", PROMPT);

    if (fgets(buffer, sizeof(buffer), stdin) == NULL)
    {
      if (feof(stdin))
      {
        return 0;
      }
      else
      {
        perror("read input failed");
        clearerr(stdin);
        continue;
      }
    }

    Str8 input = str8_copy(arena, str8_from_cstr(buffer));
    Parser p   = parse(arena, input);

    parse_print_messages(&p);

    if (p.message_list.level < MessageLevel_Error)
    {
      Object const *result = eval_program(arena, p.statements, &env);

      TmpArena scratch = scratch_begin(0);
      Str8 output      = eval_inspect(scratch.arena, result);
      if (output.len > 0) printf("%.*s\n", str8_va(output));
      scratch_end(scratch);
    }

    print_prompt = input.buf[input.len - 1] == '\n';
  }
}

////////////////////////////////////////////////////////////////////////////////
// test

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

internal int
test_lex(void)
{
  Str8 input = str8_lit("let five = 5;\n"
                        "let ten = 10;\n"
                        "\n"
                        "let add = fn(x, y) {\n"
                        "  x + y;\n"
                        "};\n"
                        "\n"
                        "let result = add(five, ten);"
                        "\n"
                        "!-/*5;\n"
                        "5 < 10 > 5\n"
                        "\n"
                        "if (5 < 10) {\n"
                        "  return true;\n"
                        "} else {\n"
                        "  return false;\n"
                        "}\n"
                        "\n"
                        "10 == 10\n"
                        "10 != 9\n"
                        "\n"
                        "[1, 2]\n"
                        "\n"
                        "@\n"
                        "\n"
                        "\"word\"\n"
                        "\"two words\"\n"
                        "\"\\\"esc\\\"\"\n"
                        "\"unclosed");

#define test_result(kind, literal) { kind, str8_lit(literal) }
  Token test_results[] = {
    test_result(TokenKind_Let, "let"),
    test_result(TokenKind_Identifier, "five"),
    test_result(TokenKind_Assign, "="),
    test_result(TokenKind_Number, "5"),
    test_result(TokenKind_Semicolon, ";"),
    test_result(TokenKind_Let, "let"),
    test_result(TokenKind_Identifier, "ten"),
    test_result(TokenKind_Assign, "="),
    test_result(TokenKind_Number, "10"),
    test_result(TokenKind_Semicolon, ";"),

    test_result(TokenKind_Let, "let"),
    test_result(TokenKind_Identifier, "add"),
    test_result(TokenKind_Assign, "="),
    test_result(TokenKind_Function, "fn"),
    test_result(TokenKind_LParen, "("),
    test_result(TokenKind_Identifier, "x"),
    test_result(TokenKind_Comma, ","),
    test_result(TokenKind_Identifier, "y"),
    test_result(TokenKind_RParen, ")"),
    test_result(TokenKind_LBrace, "{"),
    test_result(TokenKind_Identifier, "x"),
    test_result(TokenKind_Plus, "+"),
    test_result(TokenKind_Identifier, "y"),
    test_result(TokenKind_Semicolon, ";"),
    test_result(TokenKind_RBrace, "}"),
    test_result(TokenKind_Semicolon, ";"),

    test_result(TokenKind_Let, "let"),
    test_result(TokenKind_Identifier, "result"),
    test_result(TokenKind_Assign, "="),
    test_result(TokenKind_Identifier, "add"),
    test_result(TokenKind_LParen, "("),
    test_result(TokenKind_Identifier, "five"),
    test_result(TokenKind_Comma, ","),
    test_result(TokenKind_Identifier, "ten"),
    test_result(TokenKind_RParen, ")"),
    test_result(TokenKind_Semicolon, ";"),

    test_result(TokenKind_Bang, "!"),
    test_result(TokenKind_Minus, "-"),
    test_result(TokenKind_Slash, "/"),
    test_result(TokenKind_Star, "*"),
    test_result(TokenKind_Number, "5"),
    test_result(TokenKind_Semicolon, ";"),
    test_result(TokenKind_Number, "5"),
    test_result(TokenKind_Less, "<"),
    test_result(TokenKind_Number, "10"),
    test_result(TokenKind_Greater, ">"),
    test_result(TokenKind_Number, "5"),
    test_result(TokenKind_If, "if"),
    test_result(TokenKind_LParen, "("),
    test_result(TokenKind_Number, "5"),
    test_result(TokenKind_Less, "<"),
    test_result(TokenKind_Number, "10"),
    test_result(TokenKind_RParen, ")"),
    test_result(TokenKind_LBrace, "{"),
    test_result(TokenKind_Return, "return"),
    test_result(TokenKind_True, "true"),
    test_result(TokenKind_Semicolon, ";"),
    test_result(TokenKind_RBrace, "}"),
    test_result(TokenKind_Else, "else"),
    test_result(TokenKind_LBrace, "{"),
    test_result(TokenKind_Return, "return"),
    test_result(TokenKind_False, "false"),
    test_result(TokenKind_Semicolon, ";"),
    test_result(TokenKind_RBrace, "}"),

    test_result(TokenKind_Number, "10"),
    test_result(TokenKind_Equal, "=="),
    test_result(TokenKind_Number, "10"),
    test_result(TokenKind_Number, "10"),
    test_result(TokenKind_NotEqual, "!="),
    test_result(TokenKind_Number, "9"),

    test_result(TokenKind_LBracket, "["),
    test_result(TokenKind_Number, "1"),
    test_result(TokenKind_Comma, ","),
    test_result(TokenKind_Number, "2"),
    test_result(TokenKind_RBracket, "]"),

    test_result(TokenKind_Illegal, "@"),

    test_result(TokenKind_String, "\"word\""),
    test_result(TokenKind_String, "\"two words\""),
    test_result(TokenKind_StringEsc, "\"\\\"esc\\\"\""),
    test_result(TokenKind_StringOpen, "\"unclosed"),
  };
#undef test_result

  Lexer l     = { 0 };
  Token token = { 0 };

  lex_init(&l, input);
  usize tok_count = 0;

  for (lex_advance_token(&l, &token); token.kind != TokenKind_EOF;
       lex_advance_token(&l, &token), tok_count++)
  {
    Token expected = test_results[tok_count];

    test_assert_m(token.kind == expected.kind,
                  "test[%zu] - wrong token kind - expected=%.*s, actual=%.*s",
                  tok_count,
                  str8_va(token_name(expected.kind)),
                  str8_va(token_name(token.kind)));

    test_assert_m(str8_equal(token.value, expected.value),
                  "test[%zu] - wrong token value - expected='%.*s', actual='%.*s'",
                  tok_count,
                  str8_va(expected.value),
                  str8_va(token.value));
  }

  test_assert_m(arr_count(test_results) == tok_count,
                "wrong lex result count - expected=%lu, actual=%zu",
                arr_count(test_results),
                tok_count);

  return 0;
}

internal int
test_identifier_expr(AstExpr *expr, Str8 expected)
{
  test_assert_m(expr != 0, "expected identifier expression is null");
  test_assert_m(expr->tag == AstExpr_Identifier, "expected identifier expression");

  Str8 actual = expr->data.identifier.token.value;
  test_assert_m(str8_equal(actual, expected),
                "expected identifier expression '%.*s', parsed '%.*s'",
                str8_va(expected),
                str8_va(actual));

  return 0;
}

internal int
test_number_expr(AstExpr *expr, s64 expected)
{
  test_assert_m(expr != 0, "expected number expression is null");
  test_assert_m(expr->tag == AstExpr_Number, "expected number expression");

  s64 actual = expr->data.number.value;
  test_assert_m(actual == expected, "expected number expression '%lld', parsed '%lld'", expected, actual);

  return 0;
}

internal int
test_boolean_expr(AstExpr *expr, b8 expected)
{
  test_assert_m(expr != 0, "expected boolean expression is null");
  test_assert_m(expr->tag == AstExpr_Boolean, "expected boolean expression");

  b8 actual = expr->data.boolean.value;
  test_assert_m(actual == expected, "expected boolean expression '%d', parsed '%d'", expected, actual);

  return 0;
}

internal int
test_literal_expr(AstExpr *expr, AstExprKind expected_kind, void *expected)
{
  switch (expected_kind)
  {
    case AstExpr_Identifier: test_helper(test_identifier_expr(expr, *(Str8 *)expected)); break;
    case AstExpr_Number: test_helper(test_number_expr(expr, *(s64 *)expected)); break;
    case AstExpr_Boolean: test_helper(test_boolean_expr(expr, *(b8 *)expected)); break;

    default: test_fail("invalid AST expression kind for literal");
  }

  return 0;
}

typedef struct ExpectedLitExpr ExpectedLitExpr;
struct ExpectedLitExpr
{
  AstExprKind kind;
  void *value;
};

#define expected_lit(kind_, T, value_)                 \
  (ExpectedLitExpr)                                    \
  {                                                    \
    .kind = (kind_), .value = &((T[]){ (value_) })[0], \
  }

#define test_lit_expr(expr, expected) test_literal_expr((expr), (expected).kind, (expected).value)

internal int
test_prefix_lit_expr(AstExpr *expr, TokenKind expected_op, ExpectedLitExpr expected_rhs)
{
  test_assert_m(expr != 0, "expected prefix expression is null");
  test_assert_m(expr->tag == AstExpr_Prefix, "expected prefix expression");
  test_assert_m(expr->data.prefix.token.kind == expected_op,
                "expected '%.*s' infix expression, parsed '%.*s'",
                str8_va(token_name(expected_op)),
                str8_va(token_name(expr->data.prefix.token.kind)));
  test_helper(test_literal_expr(expr->data.prefix.rhs, expected_rhs.kind, expected_rhs.value));

  return 0;
}

internal int
test_infix_lit_expr(AstExpr *expr,
                    TokenKind expected_op,
                    ExpectedLitExpr expected_lhs,
                    ExpectedLitExpr expected_rhs)
{
  test_assert_m(expr != 0, "expected infix expression is null");
  test_assert_m(expr->tag == AstExpr_Infix, "expected infix expression");
  test_assert_m(expr->data.infix.token.kind == expected_op,
                "expected '%.*s' infix expression, parsed '%.*s'",
                str8_va(token_name(expected_op)),
                str8_va(token_name(expr->data.infix.token.kind)));
  test_helper(test_literal_expr(expr->data.infix.lhs, expected_lhs.kind, expected_lhs.value));
  test_helper(test_literal_expr(expr->data.infix.rhs, expected_rhs.kind, expected_rhs.value));

  return 0;
}

internal int
test_parse_check_messages(Parser *p)
{
  parse_print_messages(p);
  test_assert_m(p->message_list.level <= MessageLevel_Info, "unexpected parse errors");

  return 0;
}

internal int
test_parse_stmt_let(void)
{
  struct
  {
    Str8 input;
    Str8 expected_identifier;
    ExpectedLitExpr expected_expr;
  } tests[] = {
    { str8_lit("let x = 1;"), str8_lit("x"), expected_lit(AstExpr_Number, s64, 1) },
    { str8_lit("let y = true;"), str8_lit("y"), expected_lit(AstExpr_Boolean, b8, true) },
    { str8_lit("let z = y;"), str8_lit("z"), expected_lit(AstExpr_Identifier, Str8, str8_lit("y")) },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected let statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Let, "expected let statement");
    test_assert_m(str8_equal(stmt->data.let.identifier->token.value, tests[i].expected_identifier),
                  "expected let identifier '%.*s', parsed '%.*s'",
                  str8_va(tests[i].expected_identifier),
                  str8_va(stmt->data.let.identifier->token.value));
    test_helper(test_lit_expr(stmt->data.let.expr, tests[i].expected_expr));

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_parse_stmt_ret(void)
{
  struct
  {
    Str8 input;
    ExpectedLitExpr expected;
  } tests[] = {
    { str8_lit("return 1;"), expected_lit(AstExpr_Number, s64, 1) },
    { str8_lit("return true;"), expected_lit(AstExpr_Boolean, b8, true) },
    { str8_lit("return y;"), expected_lit(AstExpr_Identifier, Str8, str8_lit("y")) },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected return statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Ret, "expected return statement");
    test_helper(test_lit_expr(stmt->data.ret.expr, tests[i].expected));

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_parse_stmt_expr(void)
{
  Str8 input                  = str8_lit("test_identifier;");
  Str8 expected_identifiers[] = { str8_lit("test_identifier") };

  TmpArena scratch = scratch_begin(0);

  Parser p = parse(scratch.arena, input);
  test_helper(test_parse_check_messages(&p));

  usize i = 0;
  for (AstStmt *stmt = p.statements; stmt != 0; stmt = stmt->next, i++)
  {
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");
    test_helper(test_identifier_expr(stmt->data.expr.expr, expected_identifiers[i]));
  }

  test_assert_m(arr_count(expected_identifiers) == i,
                "expected %zu statements, parsed %zu",
                arr_count(expected_identifiers),
                i);

  scratch_end(scratch);

  return 0;
}

internal int
test_parse_expr_identifier(void)
{
  Str8 input                  = str8_lit("test_identifier; test_identifier2");
  Str8 expected_identifiers[] = { str8_lit("test_identifier"), str8_lit("test_identifier2") };

  TmpArena scratch = scratch_begin(0);

  Parser p = parse(scratch.arena, input);
  test_helper(test_parse_check_messages(&p));

  usize i = 0;
  for (AstStmt *stmt = p.statements; stmt != 0; stmt = stmt->next, i++)
  {
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");
    test_helper(test_identifier_expr(stmt->data.expr.expr, expected_identifiers[i]));
  }

  test_assert_m(arr_count(expected_identifiers) == i,
                "expected %zu statements, parsed %zu",
                arr_count(expected_identifiers),
                i);

  scratch_end(scratch);

  return 0;
}

internal int
test_parse_expr_number(void)
{
  Str8 input             = str8_lit("42069; 32");
  s64 expected_numbers[] = { 42069, 32 };

  TmpArena scratch = scratch_begin(0);

  Parser p = parse(scratch.arena, input);
  test_helper(test_parse_check_messages(&p));

  usize i = 0;
  for (AstStmt *stmt = p.statements; stmt != 0; stmt = stmt->next, i++)
  {
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");
    test_helper(test_number_expr(stmt->data.expr.expr, expected_numbers[i]));
  }

  test_assert_m(arr_count(expected_numbers) == i,
                "expected %zu statements, parsed %zu",
                arr_count(expected_numbers),
                i);

  scratch_end(scratch);

  return 0;
}

internal int
test_parse_expr_prefix(void)
{
  struct
  {
    Str8 input;
    TokenKind op;
    ExpectedLitExpr expected;
  } tests[] = {
    { str8_lit("!5;"), TokenKind_Bang, expected_lit(AstExpr_Number, s64, 5) },
    { str8_lit("-4;"), TokenKind_Minus, expected_lit(AstExpr_Number, s64, 4) },
    { str8_lit("!fb;"), TokenKind_Bang, expected_lit(AstExpr_Identifier, Str8, str8_lit("fb")) },
    { str8_lit("-fb;"), TokenKind_Minus, expected_lit(AstExpr_Identifier, Str8, str8_lit("fb")) },
    { str8_lit("!true;"), TokenKind_Bang, expected_lit(AstExpr_Boolean, b8, true) },
    { str8_lit("!false;"), TokenKind_Bang, expected_lit(AstExpr_Boolean, b8, false) },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected prefix statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");
    test_helper(test_prefix_lit_expr(stmt->data.expr.expr, tests[i].op, tests[i].expected));

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_parse_expr_infix(void)
{
  struct
  {
    Str8 input;
    TokenKind op;
    ExpectedLitExpr lhs;
    ExpectedLitExpr rhs;
  } tests[] = {
    {
      str8_lit("5 + 4;"),
      TokenKind_Plus,
      expected_lit(AstExpr_Number, s64, 5),
      expected_lit(AstExpr_Number, s64, 4),
    },
    {
      str8_lit("6 - 5;"),
      TokenKind_Minus,
      expected_lit(AstExpr_Number, s64, 6),
      expected_lit(AstExpr_Number, s64, 5),
    },
    {
      str8_lit("7 * 6;"),
      TokenKind_Star,
      expected_lit(AstExpr_Number, s64, 7),
      expected_lit(AstExpr_Number, s64, 6),
    },
    {
      str8_lit("8 / 7;"),
      TokenKind_Slash,
      expected_lit(AstExpr_Number, s64, 8),
      expected_lit(AstExpr_Number, s64, 7),
    },
    {
      str8_lit("9 > 8;"),
      TokenKind_Greater,
      expected_lit(AstExpr_Number, s64, 9),
      expected_lit(AstExpr_Number, s64, 8),
    },
    {
      str8_lit("8 < 9;"),
      TokenKind_Less,
      expected_lit(AstExpr_Number, s64, 8),
      expected_lit(AstExpr_Number, s64, 9),
    },
    {
      str8_lit("7 == 8;"),
      TokenKind_Equal,
      expected_lit(AstExpr_Number, s64, 7),
      expected_lit(AstExpr_Number, s64, 8),
    },
    {
      str8_lit("6 != 7;"),
      TokenKind_NotEqual,
      expected_lit(AstExpr_Number, s64, 6),
      expected_lit(AstExpr_Number, s64, 7),
    },
    {
      str8_lit("a + b;"),
      TokenKind_Plus,
      expected_lit(AstExpr_Identifier, Str8, str8_lit("a")),
      expected_lit(AstExpr_Identifier, Str8, str8_lit("b")),
    },
    {
      str8_lit("a - b;"),
      TokenKind_Minus,
      expected_lit(AstExpr_Identifier, Str8, str8_lit("a")),
      expected_lit(AstExpr_Identifier, Str8, str8_lit("b")),
    },
    {
      str8_lit("a * b;"),
      TokenKind_Star,
      expected_lit(AstExpr_Identifier, Str8, str8_lit("a")),
      expected_lit(AstExpr_Identifier, Str8, str8_lit("b")),
    },
    {
      str8_lit("a / b;"),
      TokenKind_Slash,
      expected_lit(AstExpr_Identifier, Str8, str8_lit("a")),
      expected_lit(AstExpr_Identifier, Str8, str8_lit("b")),
    },
    {
      str8_lit("a > b;"),
      TokenKind_Greater,
      expected_lit(AstExpr_Identifier, Str8, str8_lit("a")),
      expected_lit(AstExpr_Identifier, Str8, str8_lit("b")),
    },
    {
      str8_lit("a < b;"),
      TokenKind_Less,
      expected_lit(AstExpr_Identifier, Str8, str8_lit("a")),
      expected_lit(AstExpr_Identifier, Str8, str8_lit("b")),
    },
    {
      str8_lit("a == b;"),
      TokenKind_Equal,
      expected_lit(AstExpr_Identifier, Str8, str8_lit("a")),
      expected_lit(AstExpr_Identifier, Str8, str8_lit("b")),
    },
    {
      str8_lit("a != b;"),
      TokenKind_NotEqual,
      expected_lit(AstExpr_Identifier, Str8, str8_lit("a")),
      expected_lit(AstExpr_Identifier, Str8, str8_lit("b")),
    },
    {
      str8_lit("true == true"),
      TokenKind_Equal,
      expected_lit(AstExpr_Boolean, b8, true),
      expected_lit(AstExpr_Boolean, b8, true),
    },
    {
      str8_lit("true != false"),
      TokenKind_NotEqual,
      expected_lit(AstExpr_Boolean, b8, true),
      expected_lit(AstExpr_Boolean, b8, false),
    },
    {
      str8_lit("false == false"),
      TokenKind_Equal,
      expected_lit(AstExpr_Boolean, b8, false),
      expected_lit(AstExpr_Boolean, b8, false),
    },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected infix statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");
    test_helper(test_infix_lit_expr(stmt->data.expr.expr, tests[i].op, tests[i].lhs, tests[i].rhs));

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_parse_expr_if(void)
{
  Str8 input = str8_lit("if (x < y) { x }");

  TmpArena scratch = scratch_begin(0);

  Parser p = parse(scratch.arena, input);
  test_helper(test_parse_check_messages(&p));

  AstStmt *stmt = p.statements;
  test_assert_m(stmt != 0, "expected if-else statement is null");
  test_assert_m(stmt->next == 0, "expected one statement, parsed more");
  test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");

  AstExpr *expr = stmt->data.expr.expr;
  test_assert_m(expr != 0, "expected if-else expression is null");
  test_assert_m(expr->tag == AstExpr_IfElse, "expected if-else expression");
  test_helper(test_infix_lit_expr(expr->data.if_else.condition,
                                  TokenKind_Less,
                                  expected_lit(AstExpr_Identifier, Str8, str8_lit("x")),
                                  expected_lit(AstExpr_Identifier, Str8, str8_lit("y"))));

  test_assert_m(expr->data.if_else.consequence != 0, "expected consequence");
  test_assert_m(expr->data.if_else.alternative == 0, "expected no alternative");

  AstStmt *consequence = expr->data.if_else.consequence->statements;
  test_assert_m(consequence != 0, "expected one consequence statement, parsed zero");
  test_assert_m(consequence->next == 0, "expected one consequence statement, parsed more");
  test_helper(test_literal_expr(consequence->data.expr.expr, AstExpr_Identifier, &str8_lit("x")));

  scratch_end(scratch);

  return 0;
}

internal int
test_parse_expr_if_else(void)
{
  Str8 input = str8_lit("if (x < y) { x } else { y }");

  TmpArena scratch = scratch_begin(0);

  Parser p = parse(scratch.arena, input);
  test_helper(test_parse_check_messages(&p));

  AstStmt *stmt = p.statements;
  test_assert_m(stmt != 0, "expected if-else statement is null");
  test_assert_m(stmt->next == 0, "expected one statement, parsed more");
  test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");

  AstExpr *expr = stmt->data.expr.expr;
  test_assert_m(expr != 0, "expected if-else expression is null");
  test_assert_m(expr->tag == AstExpr_IfElse, "expected if/else expression");
  test_helper(test_infix_lit_expr(expr->data.if_else.condition,
                                  TokenKind_Less,
                                  expected_lit(AstExpr_Identifier, Str8, str8_lit("x")),
                                  expected_lit(AstExpr_Identifier, Str8, str8_lit("y"))));

  test_assert_m(expr->data.if_else.consequence != 0, "expected consequence");
  test_assert_m(expr->data.if_else.alternative != 0, "expected alternative");

  AstStmt *consequence = expr->data.if_else.consequence->statements;
  test_assert_m(consequence != 0, "expected one consequence statement, parsed zero");
  test_assert_m(consequence->next == 0, "expected one consequence statement, parsed more");
  test_helper(test_literal_expr(consequence->data.expr.expr, AstExpr_Identifier, &str8_lit("x")));

  AstStmt *alternative = expr->data.if_else.alternative->statements;
  test_assert_m(consequence != 0, "expected one alternative statement, parsed zero");
  test_assert_m(alternative->next == 0, "expected one alternative statement, parsed more");
  test_helper(test_literal_expr(alternative->data.expr.expr, AstExpr_Identifier, &str8_lit("y")));

  scratch_end(scratch);

  return 0;
}

internal int
test_parse_expr_function_lit(void)
{
  Str8 input = str8_lit("fn(x, y) { x + y; }");

  TmpArena scratch = scratch_begin(0);

  Parser p = parse(scratch.arena, input);
  test_helper(test_parse_check_messages(&p));

  AstStmt *stmt = p.statements;
  test_assert_m(stmt != 0, "expected function statement is null");
  test_assert_m(stmt->next == 0, "expected one statement, parsed more");
  test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");

  AstExpr *expr = stmt->data.expr.expr;
  test_assert_m(expr != 0, "expected function expression is null");
  test_assert_m(expr->tag == AstExpr_Function, "expected function expression");

  AstExprFunctionParam *param = expr->data.function.params;
  test_assert_m(param != 0, "expected two function params, parsed zero");
  test_assert_m(str8_equal(param->identifier.token.value, str8_lit("x")),
                "expected param 'x', parsed '%.*s'",
                str8_va(param->identifier.token.value));

  param = param->next;
  test_assert_m(param != 0, "expected two function params, parsed one");
  test_assert_m(str8_equal(param->identifier.token.value, str8_lit("y")),
                "expected param 'y', parsed '%.*s'",
                str8_va(param->identifier.token.value));

  param = param->next;
  test_assert_m(param == 0, "expected two function params, parsed more");

  test_assert_m(expr->data.function.body != 0, "expected body");

  AstStmt *body = expr->data.function.body->statements;
  test_assert_m(body != 0, "expected one body statement, parsed zero");
  test_assert_m(body->next == 0, "expected one body statement, parsed more");
  test_helper(test_infix_lit_expr(body->data.expr.expr,
                                  TokenKind_Plus,
                                  expected_lit(AstExpr_Identifier, Str8, str8_lit("x")),
                                  expected_lit(AstExpr_Identifier, Str8, str8_lit("y"))));

  scratch_end(scratch);

  return 0;
}

internal int
test_parse_expr_function_params(void)
{
  struct
  {
    Str8 input;
    usize expected_count;
    Str8 expected_params[3];
  } tests[] = {
    { str8_lit("fn(){}"), 0, { 0 } },
    { str8_lit("fn(x){}"), 1, { str8_lit("x") } },
    { str8_lit("fn(x,y,z){}"), 3, { str8_lit("x"), str8_lit("y"), str8_lit("z") } },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected function statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");

    AstExpr *expr = stmt->data.expr.expr;
    test_assert_m(expr != 0, "expected function expression is null");
    test_assert_m(expr->tag == AstExpr_Function, "expected function expression");

    AstExprFunctionParam *param = expr->data.function.params;
    usize expected_count        = tests[i].expected_count;

    for (usize p_index = 0; p_index < expected_count; p_index++)
    {
      test_assert_m(param != 0, "expected %zu function params, parsed %zu", expected_count, p_index);

      Str8 expected_param = tests[i].expected_params[p_index];
      test_assert_m(str8_equal(param->identifier.token.value, expected_param),
                    "expected param '%.*s', parsed '%.*s'",
                    str8_va(expected_param),
                    str8_va(param->identifier.token.value));

      param = param->next;
    }

    test_assert_m(param == 0, "expected %zu function params, parsed more", tests[i].expected_count);

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_parse_expr_call(void)
{
  Str8 input = str8_lit("add(1, 2 * 3, val);");

  TmpArena scratch = scratch_begin(0);

  Parser p = parse(scratch.arena, input);
  test_helper(test_parse_check_messages(&p));

  AstStmt *stmt = p.statements;
  test_assert_m(stmt != 0, "expected call statement is null");
  test_assert_m(stmt->next == 0, "expected one statement, parsed more");
  test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");

  AstExpr *expr = stmt->data.expr.expr;
  test_assert_m(expr != 0, "expected call expression is null");
  test_assert_m(expr->tag == AstExpr_Call, "expected call expression");

  AstExpr *fn = expr->data.call.function;
  test_assert_m(fn != 0, "expected two function params, parsed zero");
  test_assert_m(str8_equal(fn->data.identifier.token.value, str8_lit("add")),
                "expected call function 'add', parsed '%.*s'",
                str8_va(fn->data.identifier.token.value));

  AstExprCallArg *arg = expr->data.call.args;
  test_assert_m(arg != 0, "expected three function params, parsed zero");
  test_helper(test_lit_expr(arg->expr, expected_lit(AstExpr_Number, s64, 1)));

  arg = arg->next;
  test_assert_m(arg != 0, "expected three function params, parsed one");
  test_helper(test_infix_lit_expr(arg->expr,
                                  TokenKind_Star,
                                  expected_lit(AstExpr_Number, s64, 2),
                                  expected_lit(AstExpr_Number, s64, 3)));

  arg = arg->next;
  test_assert_m(arg != 0, "expected three function params, parsed two");
  test_helper(test_literal_expr(arg->expr, AstExpr_Identifier, &str8_lit("val")));

  arg = arg->next;
  test_assert_m(arg == 0, "expected three function params, parsed more");

  scratch_end(scratch);

  return 0;
}

internal int
test_parse_expr_call_args(void)
{
  struct
  {
    Str8 input;
    usize expected_count;
    ExpectedLitExpr expected_args[3];
  } tests[] = {
    { str8_lit("call();"), 0, { 0 } },
    { str8_lit("call(1);"), 1, { expected_lit(AstExpr_Number, s64, 1) } },
    {
      str8_lit("call(1,val,false);"),
      3,
      {
        expected_lit(AstExpr_Number, s64, 1),
        expected_lit(AstExpr_Identifier, Str8, str8_lit("val")),
        expected_lit(AstExpr_Boolean, b8, false),
      },
    },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected call statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");

    AstExpr *expr = stmt->data.expr.expr;
    test_assert_m(expr != 0, "expected call expression is null");
    test_assert_m(expr->tag == AstExpr_Call, "expected call expression");

    AstExprCallArg *arg  = expr->data.call.args;
    usize expected_count = tests[i].expected_count;

    for (usize a_index = 0; a_index < expected_count; a_index++)
    {
      test_assert_m(arg != 0, "expected %zu call args, parsed %zu", expected_count, a_index);
      test_helper(test_lit_expr(arg->expr, tests[i].expected_args[a_index]));

      arg = arg->next;
    }

    test_assert_m(arg == 0, "expected %zu call args, parsed more", tests[i].expected_count);

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_parse_expr_string(void)
{
  struct
  {
    Str8 input;
    Str8 expected;
  } tests[] = {
    { str8_lit("\"Hello, World!\""), str8_lit("Hello, World!") },
    { str8_lit("\"\""), str8_lit("") },
    { str8_lit("\" \\\\ \\\" \\t \\n \""), str8_lit(" \\ \" \t \n ") },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected string expression statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");

    AstExpr *expr = stmt->data.expr.expr;
    test_assert_m(expr != 0, "expected string expression is null");
    test_assert_m(expr->tag == AstExpr_String, "expected string expression");

    test_assert_m(str8_equal(expr->data.string.value, tests[i].expected),
                  "expected string '%.*s', parsed '%.*s'",
                  str8_va(tests[i].expected),
                  str8_va(expr->data.string.value));

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_parse_expr_array(void)
{
  // empty array: []
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, str8_lit("[]"));
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected array expression statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");

    AstExpr *expr = stmt->data.expr.expr;
    test_assert_m(expr != 0, "expected array expression is null");
    test_assert_m(expr->tag == AstExpr_Array, "expected array expression");
    test_assert_m(expr->data.array.elements == 0, "expected empty array, parsed elements");

    scratch_end(scratch);
  }

  // single element: [1]
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, str8_lit("[1]"));
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected array expression statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");

    AstExpr *expr = stmt->data.expr.expr;
    test_assert_m(expr != 0, "expected array expression is null");
    test_assert_m(expr->tag == AstExpr_Array, "expected array expression");

    AstExprArrayElement *element = expr->data.array.elements;
    test_assert_m(element != 0, "expected one array element, parsed zero");
    test_helper(test_lit_expr(element->value, expected_lit(AstExpr_Number, s64, 1)));
    test_assert_m(element->next == 0, "expected one array element, parsed more");

    scratch_end(scratch);
  }

  // multiple elements: [1, 1 * 2, 3 + 3]
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, str8_lit("[1, 1 * 2, 3 + 3]"));
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected array expression statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");

    AstExpr *expr = stmt->data.expr.expr;
    test_assert_m(expr != 0, "expected array expression is null");
    test_assert_m(expr->tag == AstExpr_Array, "expected array expression");

    AstExprArrayElement *element = expr->data.array.elements;
    test_assert_m(element != 0, "expected three array elements, parsed zero");
    test_helper(test_lit_expr(element->value, expected_lit(AstExpr_Number, s64, 1)));

    element = element->next;
    test_assert_m(element != 0, "expected three array elements, parsed fewer");
    test_helper(test_infix_lit_expr(element->value,
                                    TokenKind_Star,
                                    expected_lit(AstExpr_Number, s64, 1),
                                    expected_lit(AstExpr_Number, s64, 2)));

    element = element->next;
    test_assert_m(element != 0, "expected three array elements, parsed fewer");
    test_helper(test_infix_lit_expr(element->value,
                                    TokenKind_Plus,
                                    expected_lit(AstExpr_Number, s64, 3),
                                    expected_lit(AstExpr_Number, s64, 3)));

    test_assert_m(element->next == 0, "expected three array elements, parsed more");

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_parse_expr_index(void)
{
  TmpArena scratch = scratch_begin(0);

  Parser p = parse(scratch.arena, str8_lit("arr[1 + 1]"));
  test_helper(test_parse_check_messages(&p));

  AstStmt *stmt = p.statements;
  test_assert_m(stmt != 0, "expected array expression statement is null");
  test_assert_m(stmt->next == 0, "expected one statement, parsed more");
  test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");

  AstExpr *expr = stmt->data.expr.expr;
  test_assert_m(expr != 0, "expected index expression is null");
  test_assert_m(expr->tag == AstExpr_Index, "expected index expression");

  test_helper(test_identifier_expr(expr->data.index.lhs, str8_lit("arr")));
  test_helper(test_infix_lit_expr(expr->data.index.index,
                                  TokenKind_Plus,
                                  expected_lit(AstExpr_Number, s64, 1),
                                  expected_lit(AstExpr_Number, s64, 1)));

  scratch_end(scratch);

  return 0;
}

internal int
test_parse_op_precedence(void)
{
  struct
  {
    Str8 input;
    Str8 expected;
  } tests[] = {
    {
      str8_lit("-a * b"),
      str8_lit("((-a) * b)"),
    },
    {
      str8_lit("!-a"),
      str8_lit("(!(-a))"),
    },
    {
      str8_lit("a + b + c"),
      str8_lit("((a + b) + c)"),
    },
    {
      str8_lit("a + b - c"),
      str8_lit("((a + b) - c)"),
    },
    {
      str8_lit("a * b * c"),
      str8_lit("((a * b) * c)"),
    },
    {
      str8_lit("a * b / c"),
      str8_lit("((a * b) / c)"),
    },
    {
      str8_lit("a + b / c"),
      str8_lit("(a + (b / c))"),
    },
    {
      str8_lit("a + b * c + d / e - f"),
      str8_lit("(((a + (b * c)) + (d / e)) - f)"),
    },
    {
      str8_lit("3 + 4; -5 * 5"),
      str8_lit("(3 + 4)((-5) * 5)"),
    },
    {
      str8_lit("5 > 4 == 3 < 4"),
      str8_lit("((5 > 4) == (3 < 4))"),
    },
    {
      str8_lit("5 < 4 != 3 > 4"),
      str8_lit("((5 < 4) != (3 > 4))"),
    },
    {
      str8_lit("3 + 4 * 5 == 3 * 1 + 4 * 5"),
      str8_lit("((3 + (4 * 5)) == ((3 * 1) + (4 * 5)))"),
    },
    {
      str8_lit("true"),
      str8_lit("true"),
    },
    {
      str8_lit("false"),
      str8_lit("false"),
    },
    {
      str8_lit("3 > 5 == false"),
      str8_lit("((3 > 5) == false)"),
    },
    {
      str8_lit("3 < 5 == true"),
      str8_lit("((3 < 5) == true)"),
    },
    {
      str8_lit("1 + (2 + 3) + 4"),
      str8_lit("((1 + (2 + 3)) + 4)"),
    },
    {
      str8_lit("(5 + 5) * 2"),
      str8_lit("((5 + 5) * 2)"),
    },
    {
      str8_lit("2 / (5 + 5)"),
      str8_lit("(2 / (5 + 5))"),
    },
    {
      str8_lit("-(5 + 5)"),
      str8_lit("(-(5 + 5))"),
    },
    {
      str8_lit("!(true == true)"),
      str8_lit("(!(true == true))"),
    },

    {
      str8_lit("a + add(b * c) + d"),
      str8_lit("((a + add((b * c))) + d)"),
    },
    {
      str8_lit("add(a, b, 1, 2 * 3, 4 + 5, add(6, 7 * 8))"),
      str8_lit("add(a, b, 1, (2 * 3), (4 + 5), add(6, (7 * 8)))"),
    },
    {
      str8_lit("add(a + b + c * d / f + g)"),
      str8_lit("add((((a + b) + ((c * d) / f)) + g))"),
    },
    {
      str8_lit("a * [1, 2, 3, 4][b * c] * d"),
      str8_lit("((a * ([1, 2, 3, 4][(b * c)])) * d)"),
    },
    {
      str8_lit("add(a * b[2], b[1], 2 * [1, 2][1])"),
      str8_lit("add((a * (b[2])), (b[1]), (2 * ([1, 2][1])))"),
    },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    StrBuilder8 b = str8_builder_create_default(scratch.arena);
    parse_build_stmt_string(&b, p.statements);
    Str8 actual = str8_build(&b);

    test_assert_m(str8_equal(tests[i].expected, actual),
                  "expected precedence of '%.*s', parsed '%.*s'",
                  str8_va(tests[i].expected),
                  str8_va(actual));

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_env_set_get(void)
{
  TmpArena scratch = scratch_begin(0);

  Env env = { 0 };
  env_init(&env, scratch.arena, 0);

  Str8 name           = str8_lit("x");
  Object const *value = object_from_number(scratch.arena, 5);
  env_set(&env, name, value);

  test_assert_m(env.cnt == 1, "expected env count of 1, got %u", env.cnt);

  Object const *got = env_get(&env, name);
  test_assert_m(got != 0, "expected to find '%.*s' in environment", str8_va(name));
  test_assert_m(got == value, "expected env_get to return inserted value pointer");

  scratch_end(scratch);
  return 0;
}

internal int
test_env_get_missing(void)
{
  TmpArena scratch = scratch_begin(0);

  Env env = { 0 };
  env_init(&env, scratch.arena, 0);

  test_assert_m(env_get(&env, str8_lit("missing")) == 0,
                "expected null result for missing identifier in empty env");

  env_set(&env, str8_lit("present"), object_from_number(scratch.arena, 1));
  test_assert_m(env_get(&env, str8_lit("missing")) == 0,
                "expected null result for missing identifier in populated env");

  scratch_end(scratch);
  return 0;
}

internal int
test_env_overwrite(void)
{
  TmpArena scratch = scratch_begin(0);

  Env env = { 0 };
  env_init(&env, scratch.arena, 0);

  Str8 name            = str8_lit("x");
  Object const *first  = object_from_number(scratch.arena, 1);
  Object const *second = object_from_number(scratch.arena, 2);

  env_set(&env, name, first);
  env_set(&env, name, second);

  test_assert_m(env.cnt == 1, "expected count of 1 after overwrite, got %u", env.cnt);

  Object const *got = env_get(&env, name);
  test_assert_m(got == second, "expected env_get to return most recently set value");

  scratch_end(scratch);
  return 0;
}

internal int
test_env_outer_scope(void)
{
  TmpArena scratch = scratch_begin(0);

  Env outer = { 0 };
  env_init(&outer, scratch.arena, 0);
  env_set(&outer, str8_lit("outer_only"), object_from_number(scratch.arena, 1));
  env_set(&outer, str8_lit("shadowed"), object_from_number(scratch.arena, 2));

  Env inner = { 0 };
  env_init(&inner, scratch.arena, 0);
  inner.outer = &outer;
  env_set(&inner, str8_lit("inner_only"), object_from_number(scratch.arena, 3));
  env_set(&inner, str8_lit("shadowed"), object_from_number(scratch.arena, 4));

  Object const *o = env_get(&inner, str8_lit("outer_only"));
  test_assert_m(o != 0, "expected to find outer-only identifier via inner scope");
  test_assert_m(o->tag == ObjectKind_Number, "expected number object for outer-only identifier");
  test_assert_m(o->data.number.value == 1, "expected outer-only value 1, got %lld", o->data.number.value);

  Object const *i = env_get(&inner, str8_lit("inner_only"));
  test_assert_m(i != 0, "expected to find inner-only identifier in inner scope");
  test_assert_m(i->tag == ObjectKind_Number, "expected number object for inner-only identifier");
  test_assert_m(i->data.number.value == 3, "expected inner-only value 3, got %lld", i->data.number.value);

  Object const *s = env_get(&inner, str8_lit("shadowed"));
  test_assert_m(s != 0, "expected to find shadowed identifier in inner scope");
  test_assert_m(s->tag == ObjectKind_Number, "expected number object for shadowed identifier");
  test_assert_m(s->data.number.value == 4,
                "expected inner scope to shadow outer value, got %lld",
                s->data.number.value);

  Object const *os = env_get(&outer, str8_lit("shadowed"));
  test_assert_m(os != 0, "expected to find shadowed identifier in outer scope");
  test_assert_m(os->data.number.value == 2,
                "expected outer scope's shadowed value to be unchanged, got %lld",
                os->data.number.value);

  test_assert_m(env_get(&outer, str8_lit("inner_only")) == 0,
                "expected inner-only identifier to be invisible from outer scope");

  scratch_end(scratch);
  return 0;
}

internal int
test_env_grow(void)
{
  TmpArena scratch = scratch_begin(0);

  Env env = { 0 };
  env_init(&env, scratch.arena, 0);
  u32 initial_cap = env.cap;

  enum
  {
    insert_count = 64
  };
  Str8 names[insert_count];

  for (s64 i = 0; i < insert_count; i++)
  {
    StrBuilder8 b = str8_builder_create_default(scratch.arena);
    str8_append_f(&b, "var_%lld", i);
    names[i] = str8_build(&b);
    env_set(&env, names[i], object_from_number(scratch.arena, i));
  }

  test_assert_m(env.cap > initial_cap,
                "expected capacity to grow beyond %u after %d inserts, got %u",
                initial_cap,
                insert_count,
                env.cap);
  test_assert_m(env.cnt == insert_count,
                "expected count of %d after inserts, got %u",
                insert_count,
                env.cnt);

  for (s64 i = 0; i < insert_count; i++)
  {
    Object const *got = env_get(&env, names[i]);
    test_assert_m(got != 0, "expected to find '%.*s' in grown env", str8_va(names[i]));
    test_assert_m(got->tag == ObjectKind_Number, "expected number object for '%.*s'", str8_va(names[i]));
    test_assert_m(got->data.number.value == i,
                  "expected value %lld for '%.*s', got %lld",
                  i,
                  str8_va(names[i]),
                  got->data.number.value);
  }

  scratch_end(scratch);
  return 0;
}

internal int
test_eval_number_expr(void)
{
  struct
  {
    Str8 input;
    s64 expected;
  } tests[] = {
    { str8_lit("7"), 7 },
    { str8_lit("29"), 29 },
    { str8_lit("-7"), -7 },
    { str8_lit("-29"), -29 },
    { str8_lit("5 + 5 + 5 + 5 - 10"), 10 },
    { str8_lit("2 * 2 * 2 * 2 * 2"), 32 },
    { str8_lit("-50 + 100 + -50"), 0 },
    { str8_lit("5 * 2 + 10"), 20 },
    { str8_lit("5 + 2 * 10"), 25 },
    { str8_lit("20 + 2 * -10"), 0 },
    { str8_lit("50 / 2 * 2 + 10"), 60 },
    { str8_lit("2 * (5 + 10)"), 30 },
    { str8_lit("3 * 3 * 3 + 10"), 37 },
    { str8_lit("3 * (3 * 3) + 10"), 37 },
    { str8_lit("(5 + 10 * 2 + 15 / 3) * 2 + -10"), 50 },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_Number, "expected number object");
    test_assert_m(result->data.number.value == tests[i].expected,
                  "expected number value '%lld', evaluated '%lld'",
                  tests[i].expected,
                  result->data.number.value);

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_eval_boolean_expr(void)
{
  struct
  {
    Str8 input;
    b8 expected;
  } tests[] = {
    { str8_lit("true"), true },
    { str8_lit("false"), false },
    { str8_lit("1 < 2"), true },
    { str8_lit("1 > 2"), false },
    { str8_lit("1 < 1"), false },
    { str8_lit("1 > 1"), false },
    { str8_lit("1 == 1"), true },
    { str8_lit("1 != 1"), false },
    { str8_lit("1 == 2"), false },
    { str8_lit("1 != 2"), true },
    { str8_lit("true == true"), true },
    { str8_lit("false == false"), true },
    { str8_lit("true == false"), false },
    { str8_lit("true != false"), true },
    { str8_lit("false != true"), true },
    { str8_lit("(1 < 2) == true"), true },
    { str8_lit("(1 < 2) == false"), false },
    { str8_lit("(1 > 2) == true"), false },
    { str8_lit("(1 > 2) == false"), true },
    { str8_lit("\"hello\" == \"hello\""), true },
    { str8_lit("\"hello\" == \"ello\""), false },
    { str8_lit("\"hello\" != \"hello\""), false },
    { str8_lit("\"hello\" != \"ello\""), true },
    { str8_lit("\" \\? \t\n \" == \" \\? \t\n \""), true },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_Boolean, "expected boolean object");
    test_assert_m(result->data.boolean.value == tests[i].expected,
                  "expected boolean value '%d', evaluated '%d'",
                  tests[i].expected,
                  result->data.boolean.value);

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_eval_bang(void)
{
  struct
  {
    Str8 input;
    b8 expected;
  } tests[] = {
    { str8_lit("!true"), false },   { str8_lit("!false"), true }, { str8_lit("!!true"), true },
    { str8_lit("!!false"), false }, { str8_lit("!4"), false },    { str8_lit("!!4"), true },
    { str8_lit("!!!4"), false },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_Boolean, "expected boolean object");
    test_assert_m(result->data.boolean.value == tests[i].expected,
                  "expected boolean value '%d', evaluated '%d'",
                  tests[i].expected,
                  result->data.boolean.value);

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_eval_if_else_expr(void)
{
  struct
  {
    Str8 input;
    bool is_null;
    union
    {
      s64 num;
      Object const *nul;
    } expected;
  } tests[] = {
    { str8_lit("if (true) { 10 }"), .expected = { 10 } },
    { str8_lit("if (false) { 10 }"), .is_null = true, .expected = { .nul = OBJECT_NULL } },
    { str8_lit("if (1) { 10 }"), .expected = { 10 } },
    { str8_lit("if (1 < 2) { 10 }"), .expected = { 10 } },
    { str8_lit("if (1 > 2) { 10 }"), .is_null = true, .expected = { .nul = OBJECT_NULL } },
    { str8_lit("if (1 > 2) { 10 } else { 20 }"), .expected = { 20 } },
    { str8_lit("if (1 < 2) { 10 } else { 20 }"), .expected = { 10 } },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    if (!tests[i].is_null)
    {
      test_assert_m(result->tag == ObjectKind_Number, "expected number object");
      test_assert_m(result->data.number.value == tests[i].expected.num,
                    "expected number value '%lld', evaluated '%lld'",
                    tests[i].expected.num,
                    result->data.number.value);
    }
    else
    {
      test_assert_m(result->tag == ObjectKind_Null, "expected null");
      test_assert_m(result == tests[i].expected.nul,
                    "expected null but evaluated to different reference");
    }

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_eval_function_expr(void)
{
  struct
  {
    Str8 input;
    s64 expected;
  } tests[] = {
    { str8_lit("let identity = fn(x) { x; }; identity(5);"), 5 },
    { str8_lit("let identity = fn(x) { return x; }; identity(5);"), 5 },
    { str8_lit("let double = fn(x) { x * 2; }; double(5);"), 10 },
    { str8_lit("let add = fn(x, y) { x + y; }; add(5, 5);"), 10 },
    { str8_lit("let add = fn(x, y) { x + y; }; add(5 + 5, add(5, 5));"), 20 },
    { str8_lit("fn(x) { x; }(5)"), 5 },
    { str8_lit("fn(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z) {"
               "a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q + r + s + t + u "
               "+ v + w + x + y + z;"
               "}(1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6)"),
      111 },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_Number, "expected number object");
    test_assert_m(result->data.number.value == tests[i].expected,
                  "expected number value '%lld', evaluated '%lld'",
                  tests[i].expected,
                  result->data.number.value);

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_eval_string_expr(void)
{
  struct
  {
    Str8 input;
    Str8 expected;
  } tests[] = {
    { str8_lit("\"Hello, World!\""), str8_lit("Hello, World!") },
    { str8_lit("\"Hello\" + \", \" + \"World!\""), str8_lit("Hello, World!") },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_String, "expected string object");
    test_assert_m(str8_equal(result->data.string.value, tests[i].expected),
                  "expected string value '%.*s', evaluated '%.*s'",
                  str8_va(tests[i].expected),
                  str8_va(result->data.string.value));

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_eval_array_expr(void)
{
  // empty array: []
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, str8_lit("[]"));
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_Array, "expected array object");
    test_assert_m(result->data.array.len == 0,
                  "expected empty array, evaluated %zu elements",
                  result->data.array.len);

    scratch_end(scratch);
  }

  // one element: [1]
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, str8_lit("[1]"));
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_Array, "expected array object");
    test_assert_m(result->data.array.len == 1,
                  "expected three array elements, evaluated %zu",
                  result->data.array.len);

    Object const *element = result->data.array.elements[0];
    test_assert_m(element->tag == ObjectKind_Number, "expected number array element");
    test_assert_m(element->data.number.value == 1,
                  "expected array element '%d', evaluated '%lld'",
                  1,
                  element->data.number.value);
  }

  // multiple elements: [1, 2 * 2, 3 + 3, 7, 8, 9]
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, str8_lit("[1, 2 * 2, 3 + 3, 7, 8, 9]"));
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_Array, "expected array object");
    test_assert_m(result->data.array.len == 6,
                  "expected three array elements, evaluated %zu",
                  result->data.array.len);

    s64 expected[] = { 1, 4, 6, 7, 8, 9 };
    for (usize i = 0; i < arr_count(expected); i++)
    {
      Object const *element = result->data.array.elements[i];
      test_assert_m(element->tag == ObjectKind_Number, "expected number array element");
      test_assert_m(element->data.number.value == expected[i],
                    "expected array element '%lld', evaluated '%lld'",
                    expected[i],
                    element->data.number.value);
    }

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_eval_index_expr(void)
{
  struct
  {
    Str8 input;
    s64 expected;
    b8 is_null;
  } tests[] = {
    { str8_lit("[1, 2, 3][0]"), 1, false },
    { str8_lit("[1, 2, 3][1]"), 2, false },
    { str8_lit("[1, 2, 3][2]"), 3, false },
    { str8_lit("let i = 0; [1][i];"), 1, false },
    { str8_lit("[1, 2, 3][1 + 1];"), 3, false },
    { str8_lit("let myArray = [1, 2, 3]; myArray[2];"), 3, false },
    { str8_lit("let myArray = [1, 2, 3]; myArray[0] + myArray[1] + myArray[2];"), 6, false },
    { str8_lit("let myArray = [1, 2, 3]; let i = myArray[0]; myArray[i]"), 2, false },
    { str8_lit("[1, 2, 3][3]"), 0, true },
    { str8_lit("[1, 2, 3][-1]"), 0, true },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);

    if (tests[i].is_null)
    {
      test_assert_m(result->tag == ObjectKind_Null,
                    "expected null object, evaluated '%.*s'",
                    str8_va(object_name(result->tag)));
    }
    else
    {
      test_assert_m(result->tag == ObjectKind_Number, "expected number object");
      test_assert_m(result->data.number.value == tests[i].expected,
                    "expected number value '%lld', evaluated '%lld'",
                    tests[i].expected,
                    result->data.number.value);
    }

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_eval_builtin_len(void)
{
  struct
  {
    Str8 input;
    s64 expected;
    Str8 err;
  } tests[] = {
    {
      str8_lit("len(\"\")"),
      .expected = 0,
    },
    {
      str8_lit("len(\"four\")"),
      .expected = 4,
    },
    {
      str8_lit("len(\"hello world\")"),
      .expected = 11,
    },
    {
      str8_lit("len(1)"),
      .err = str8_lit("argument to builtin 'len' not supported: Number"),
    },
    {
      str8_lit("len()"),
      .err = str8_lit("builtin 'len' requires 1 argument, received: 0"),
    },
    {
      str8_lit("len(\"one\", \"two\")"),
      .err = str8_lit("builtin 'len' requires 1 argument, received: 2"),
    },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    if (tests[i].err.buf == 0)
    {
      test_assert_m(result->tag == ObjectKind_Number, "expected number object");
      test_assert_m(result->data.number.value == tests[i].expected,
                    "expected number value '%lld', evaluated '%lld'",
                    tests[i].expected,
                    result->data.number.value);
    }
    else
    {
      test_assert_m(result->tag == ObjectKind_Error, "expected error object");
      test_assert_m(str8_equal(tests[i].err, result->data.err.value),
                    "expected error '%.*s', evaluated '%.*s'",
                    str8_va(tests[i].err),
                    str8_va(result->data.err.value));
    }

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_eval_return_stmt(void)
{
  struct
  {
    Str8 input;
    s64 expected;
  } tests[] = {
    { str8_lit("return 10;"), 10 },
    { str8_lit("return 10; 9;"), 10 },
    { str8_lit("return 2 * 5; 9;"), 10 },
    { str8_lit("9; return 2 * 5; 9;"), 10 },
    { str8_lit("if (true) { if (true) { return 10; } } return 0;"), 10 },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_Number, "expected number object");
    test_assert_m(result->data.number.value == tests[i].expected,
                  "expected number value '%lld', evaluated '%lld'",
                  tests[i].expected,
                  result->data.number.value);

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_eval_let_stmt(void)
{
  struct
  {
    Str8 input;
    s64 expected;
  } tests[] = {
    { str8_lit("let a = 5; a;"), 5 },
    { str8_lit("let a = 5 * 5; a;"), 25 },
    { str8_lit("let a = 5; let b = a; b;"), 5 },
    { str8_lit("let a = 5; let b = a; let c = a + b + 5; c;"), 15 },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_Number, "expected number object");
    test_assert_m(result->data.number.value == tests[i].expected,
                  "expected number value '%lld', evaluated '%lld'",
                  tests[i].expected,
                  result->data.number.value);

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_eval_error_handling(void)
{
  struct
  {
    Str8 input;
    Str8 expected;
  } tests[] = {
    {
      str8_lit("5 + true;"),
      str8_lit("type mismatch: Number + Boolean"),
    },
    {
      str8_lit("5 + true; 5;"),
      str8_lit("type mismatch: Number + Boolean"),
    },
    {
      str8_lit("-true"),
      str8_lit("unknown operator: -Boolean"),
    },
    {
      str8_lit("true + false;"),
      str8_lit("unknown operator: Boolean + Boolean"),
    },
    {
      str8_lit("5; true + false; 5"),
      str8_lit("unknown operator: Boolean + Boolean"),
    },
    {
      str8_lit("if (10 > 1) { true + false; }"),
      str8_lit("unknown operator: Boolean + Boolean"),
    },
    {
      str8_lit("foobar"),
      str8_lit("identifier not found: foobar"),
    },
    {
      str8_lit("let f = fn() { x; }; f();"),
      str8_lit("identifier not found: x"),
    },
    {
      str8_lit("\"Hello\" - \"World\""),
      str8_lit("unknown operator: String - String"),
    },
    {
      str8_lit("[1,2,3][\"i\"]"),
      str8_lit("invalid array index type: String"),
    },
    {
      str8_lit("5[0]"),
      str8_lit("indexing not supported for type: Number"),
    },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_Error, "expected error object");
    test_assert_m(str8_equal(tests[i].expected, result->data.err.value),
                  "expected error '%.*s', evaluated '%.*s'",
                  str8_va(tests[i].expected),
                  str8_va(result->data.err.value));

    scratch_end(scratch);
  }

  return 0;
}

internal int
test(void)
{
  env_builtin_init(tl_scratch_arena_get(0, 0));

  int result = 0;

  result += test_lex();

  result += test_parse_stmt_let();
  result += test_parse_stmt_ret();
  result += test_parse_stmt_expr();

  result += test_parse_expr_identifier();
  result += test_parse_expr_number();
  result += test_parse_expr_prefix();
  result += test_parse_expr_infix();
  result += test_parse_expr_if();
  result += test_parse_expr_if_else();
  result += test_parse_expr_function_lit();
  result += test_parse_expr_function_params();
  result += test_parse_expr_call();
  result += test_parse_expr_call_args();
  result += test_parse_expr_string();
  result += test_parse_expr_array();
  result += test_parse_expr_index();

  result += test_parse_op_precedence();

  result += test_env_set_get();
  result += test_env_get_missing();
  result += test_env_overwrite();
  result += test_env_outer_scope();
  result += test_env_grow();

  result += test_eval_number_expr();
  result += test_eval_boolean_expr();
  result += test_eval_bang();
  result += test_eval_if_else_expr();
  result += test_eval_function_expr();
  result += test_eval_string_expr();
  result += test_eval_array_expr();
  result += test_eval_index_expr();

  result += test_eval_builtin_len();

  result += test_eval_return_stmt();
  result += test_eval_let_stmt();

  result += test_eval_error_handling();

  return result;
}
