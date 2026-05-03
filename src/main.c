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

#define min(A, B)      (((A) < (B)) ? (A) : (B))
#define max(A, B)      (((A) > (B)) ? (A) : (B))
#define clamp(A, X, B) (((X) < (A)) ? (A) : ((X) > (B)) ? (B) : (X))

#define align_up(p, a) (((usize)(p) + ((usize)(a) - 1)) & (~((usize)(a) - 1)))

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

#if (defined(_MSC_VER) && _MSC_VER < 1800) || (!defined(_MSC_VER) && !defined(__STDC_VERSION__))
#  ifndef true
#    define true (0 == 0)
#  endif
#  ifndef false
#    define false (0 != 0)
#  endif
typedef b8 bool;
#else
#  include <stdbool.h>
#endif

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
  }

#define sll_builder_init(b)            \
  do                                   \
  {                                    \
    (b).tail          = &(b).sentinel; \
    (b).sentinel.next = 0;             \
  } while (0)

#define sll_builder_append(b, n) sll_append((b).tail, (n))

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

internal TmpArena
scratch_begin(Arena *conflict)
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
    if (tl_scratch_arenas[i] != conflict)
    {
      return arena_tmp_begin(tl_scratch_arenas[i]);
    }
  }

  assert(0 && "No non-conflicting scratch arena available.");
  return (TmpArena){ 0 };
}

#define scratch_end(scratch) arena_tmp_end(scratch)

#pragma clang diagnostic pop

////////////////////////////////////////////////////////////////////////////////
// strings

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"

internal usize
cstr_length(char const *c)
{
  char const *p = c;
  for (; *p != 0; p++);
  return (usize)(p - c);
}

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
  return str8((u8 const *)c, cstr_length(c));
}

internal b32
str8_equal(Str8 a, Str8 b)
{
  return a.len == b.len && memcmp(a.buf, b.buf, a.len) == 0;
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
str8_build_to_arena(StrBuilder8 *builder, Arena *arena)
{
  u8 *buf = arena_alloc_tn_nz(arena, u8, builder->len + 1);
  memcpy(buf, builder->buf, builder->len);
  buf[builder->len] = 0;

  return str8(buf, builder->len);
}

#pragma clang diagnostic pop

////////////////////////////////////////////////////////////////////////////////
// lex

internal b32
is_whitespace(u8 ch)
{
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\v' || ch == '\f';
}

internal b32
is_digit(u8 ch)
{
  return '0' <= ch && ch <= '9';
}

internal b32
is_letter(u8 ch)
{
  return ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z');
}

// TODO(tad): Use ASCII values for corresponding token kind values?
#define TOKEN_KINDS(X)         \
  X(Illegal)                   \
  X(EOF)                       \
                               \
  /* identifiers & literals */ \
  X(Identifier)                \
  X(Number)                    \
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
  TokenKind_COUNT
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
    case ',': lex_init_token(l, token, 1, TokenKind_Comma); break;

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
  MessageLevel_COUNT
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
  Precedence_COUNT
} Precedence;

#define AST_EXPR_KINDS(X) \
  X(Identifier)           \
  X(Number)               \
  X(Boolean)              \
  X(Prefix)               \
  X(Infix)                \
  X(IfElse)               \
  X(Function)             \
  X(Call)

typedef enum
{
#define AST_EXPR_KIND(name) AstExpr_##name,
  AST_EXPR_KINDS(AST_EXPR_KIND)
#undef AST_EXPR_KIND
  AstExpr_COUNT
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
  Token token;
  AstStmt *statements;
};

typedef struct AstExprIdentifier AstExprIdentifier;
struct AstExprIdentifier
{
  Token token;
};

typedef struct AstExprNumber AstExprNumber;
struct AstExprNumber
{
  Token token;
  s64 value;
};

typedef struct AstExprBoolean AstExprBoolean;
struct AstExprBoolean
{
  Token token;
  b8 value;
};

typedef struct AstExprPrefix AstExprPrefix;
struct AstExprPrefix
{
  Token token;
  AstExpr *rhs;
};

typedef struct AstExprInfix AstExprInfix;
struct AstExprInfix
{
  Token token;
  AstExpr *lhs;
  AstExpr *rhs;
};

typedef struct AstExprIfElse AstExprIfElse;
struct AstExprIfElse
{
  Token token;
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
  Token token;
  AstExprFunctionParam *params;
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
  Token token;
  AstExpr *function;
  AstExprCallArg *args;
};

struct AstExpr
{
  AstExprKind tag;
  union
  {
    AstExprIdentifier identifier;
    AstExprNumber number;
    AstExprBoolean boolean;
    AstExprPrefix prefix;
    AstExprInfix infix;
    AstExprIfElse if_else;
    AstExprFunction function;
    AstExprCall call;
  } data;
};

typedef enum
{
  AstStmt_Let,
  AstStmt_Ret,
  AstStmt_Expr,
  AstStmt_COUNT
} AstStmtKind;

typedef struct AstStmtLet AstStmtLet;
struct AstStmtLet
{
  Token token;
  AstExprIdentifier *identifier;
  AstExpr *expr;
};

typedef struct AstStmtReturn AstStmtReturn;
struct AstStmtReturn
{
  Token token;
  AstExpr *expr;
};

typedef struct AstStmtExpr AstStmtExpr;
struct AstStmtExpr
{
  Token token;
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
parse_init(Parser *p, Str8 input, Arena *arena)
{
  p->arena = arena;

  lex_init(&p->l, input);
  lex_advance_token(&p->l, &p->curr_token);
  lex_advance_token(&p->l, &p->peek_token);
}

internal void
parse_free(Parser *p)
{
  arena_free(p->arena);
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
      str8_append_lit(b, "if(");
      parse_build_expr_string(b, expr->data.if_else.condition);
      str8_append_lit(b, "){ ");
      parse_build_stmt_string(b, expr->data.if_else.consequence->statements);
      str8_append_lit(b, " }");
      if (expr->data.if_else.alternative != 0)
      {
        str8_append_lit(b, "else{ ");
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
          str8_append_lit(b, ",");
        }
      }
      str8_append_lit(b, "){ ");
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
          str8_append_lit(b, ",");
        }
      }
      str8_append_lit(b, ")");
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

// NOTE(tad): This is entirely for debugging/testing purposes and should not otherwise be used.
internal Str8
parse_to_string(Parser *p)
{
  StrBuilder8 b = str8_builder_create(p->arena, KiB(1));
  parse_build_stmt_string(&b, p->statements);

  return str8_build(&b);
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

internal b32
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

    default: return Precedence_Lowest;
  }
}

internal AstStmt *parse_stmt(Parser *p);
internal AstExpr *parse_expr(Parser *p, Precedence precedence);
internal AstStmtBlock *parse_block(Parser *p);

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

// TODO(tad): Consider refactoring various parts of this function:
// - Use scratch arena instead of goto rollbacks.
// - Pull prefix and infix into separate functions.
// - Create creation functions for each expression type.
internal AstExpr *
parse_expr(Parser *p, Precedence precedence)
{
  AstExpr *lhs = 0;

  // prefix
  {
    TmpArena tmp;

    switch (p->curr_token.kind)
    {
      case TokenKind_Identifier:
      {
        lhs                        = arena_alloc_t(p->arena, AstExpr);
        lhs->tag                   = AstExpr_Identifier;
        lhs->data.identifier.token = p->curr_token;
        break;
      }

      case TokenKind_Number:
      {
        u8 buf[64];
        usize len = min(p->curr_token.value.len, sizeof(buf) - 1);
        memcpy(buf, p->curr_token.value.buf, len);
        buf[len] = 0;

        char *end;
        errno = 0;

        s64 number_value = strtoll((char *)buf, &end, 10);

        if (errno == ERANGE)
        {
          Str8 error =
          str8_f(p->arena, "'%.*s' is not a number literal.", str8_va(p->curr_token.value));
          parse_error(p, MessageLevel_Error, error);
        }
        else if (end == (char *)buf)
        {
          Str8 error =
          str8_f(p->arena, "Number literal '%.*s' is out of range.", str8_va(p->curr_token.value));
          parse_error(p, MessageLevel_Error, error);
        }
        else
        {
          lhs                    = arena_alloc_t(p->arena, AstExpr);
          lhs->tag               = AstExpr_Number;
          lhs->data.number.token = p->curr_token;
          lhs->data.number.value = number_value;
        }
        break;
      }

      case TokenKind_True:
      case TokenKind_False:
      {
        lhs                     = arena_alloc_t(p->arena, AstExpr);
        lhs->tag                = AstExpr_Boolean;
        lhs->data.boolean.token = p->curr_token;
        lhs->data.boolean.value = p->curr_token.kind == TokenKind_True;
        break;
      }

      case TokenKind_Minus:
      case TokenKind_Bang:
      {
        tmp = arena_tmp_begin(p->arena);

        AstExpr *prefix_expr           = arena_alloc_t(p->arena, AstExpr);
        prefix_expr->tag               = AstExpr_Prefix;
        prefix_expr->data.prefix.token = p->curr_token;
        parse_advance_token(p);

        prefix_expr->data.prefix.rhs = parse_expr(p, Precedence_Prefix);
        if (prefix_expr->data.prefix.rhs == 0) goto rollback_tmp;

        lhs = prefix_expr;
        break;
      }

      case TokenKind_If:
      {
        tmp = arena_tmp_begin(p->arena);

        AstExpr *if_expr;
        Token if_token = p->curr_token;

        if (!parse_expect(p, TokenKind_LParen)) goto rollback_tmp;
        if_expr = arena_alloc_t(p->arena, AstExpr);
        parse_advance_token(p);
        AstExpr *condition = parse_expr(p, Precedence_Lowest);
        if (condition == 0) goto rollback_tmp;
        if (!parse_expect(p, TokenKind_RParen)) goto rollback_tmp;

        if (!parse_expect(p, TokenKind_LBrace)) goto rollback_tmp;
        AstStmtBlock *consequence = parse_block(p);

        AstStmtBlock *alternative = 0;
        if (p->peek_token.kind == TokenKind_Else)
        {
          parse_advance_token(p);
          if (!parse_expect(p, TokenKind_LBrace)) goto rollback_tmp;
          alternative = parse_block(p);
        }

        lhs                           = if_expr;
        lhs->tag                      = AstExpr_IfElse;
        lhs->data.if_else.token       = if_token;
        lhs->data.if_else.condition   = condition;
        lhs->data.if_else.consequence = consequence;
        lhs->data.if_else.alternative = alternative;
        break;
      }

      case TokenKind_Function:
      {
        tmp = arena_tmp_begin(p->arena);

        AstExpr *fn_expr;
        Token fn_token = p->curr_token;

        if (!parse_expect(p, TokenKind_LParen)) goto rollback_tmp;

        fn_expr = arena_alloc_t(p->arena, AstExpr);

        SLL_BUILDER(AstExprFunctionParam) params;
        sll_builder_init(params);

        if (p->peek_token.kind != TokenKind_RParen)
        {
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
              goto rollback_tmp;
            }
          }
        }
        else
        {
          parse_advance_token(p);
        }

        if (!parse_expect(p, TokenKind_LBrace)) goto rollback_tmp;
        AstStmtBlock *body = parse_block(p);

        lhs                       = fn_expr;
        lhs->tag                  = AstExpr_Function;
        lhs->data.function.token  = fn_token;
        lhs->data.function.params = sll_builder_result(params);
        lhs->data.function.body   = body;
        break;
      }

      case TokenKind_LParen:
      {
        tmp = arena_tmp_begin(p->arena);

        parse_advance_token(p);
        AstExpr *grouped_expr = parse_expr(p, Precedence_Lowest);
        if (!parse_expect(p, TokenKind_RParen)) goto rollback_tmp;
        lhs = grouped_expr;
        break;
      }

      default:
      {
        Str8 error = str8_f(p->arena,
                            "'%.*s' (type '%.*s') is not a valid prefix token.",
                            str8_va(p->curr_token.value),
                            str8_va(token_name(p->curr_token.kind)));
        parse_error(p, MessageLevel_Error, error);
        return lhs;
      }
    }

    if (false)
    {
    rollback_tmp:
      arena_tmp_end(tmp);
    }
  }

  // infix
  while (p->curr_token.kind != TokenKind_Semicolon &&
         precedence < parse_get_precedence(p->peek_token.kind))
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
      case TokenKind_Greater:
      {
        parse_advance_token(p);

        AstExpr *infix          = arena_alloc_t(p->arena, AstExpr);
        infix->tag              = AstExpr_Infix;
        infix->data.infix.token = p->curr_token;
        infix->data.infix.lhs   = lhs;

        // NOTE(tad): Decreasing the infix precedence here is one way to support
        // right-associative operators.
        Precedence infix_precedence = parse_get_precedence(p->curr_token.kind);
        parse_advance_token(p);
        infix->data.infix.rhs = parse_expr(p, infix_precedence);

        lhs = infix;
        break;
      }

      case TokenKind_LParen:
      {
        parse_advance_token(p);

        Token call_token = p->curr_token;

        SLL_BUILDER(AstExprCallArg) args;
        sll_builder_init(args);

        if (p->peek_token.kind != TokenKind_RParen)
        {
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
        }
        else
        {
          parse_advance_token(p);
        }

        AstExpr *call            = arena_alloc_t(p->arena, AstExpr);
        call->tag                = AstExpr_Call;
        call->data.call.token    = call_token;
        call->data.call.function = lhs;
        call->data.call.args     = sll_builder_result(args);

        lhs = call;
        break;
      }

      default: return lhs;
    }
  }

  return lhs;
}

internal AstStmt *
parse_stmt(Parser *p)
{
  AstStmt *stmt = 0;

  switch (p->curr_token.kind)
  {
    // TODO(tad): Could provide better error messages if invalid expr tokens are handled here.
    case TokenKind_Identifier:
    case TokenKind_Number:
    case TokenKind_Assign:
    case TokenKind_Plus:
    case TokenKind_Minus:
    case TokenKind_Bang:
    case TokenKind_Star:
    case TokenKind_Slash:
    case TokenKind_Less:
    case TokenKind_Greater:
    case TokenKind_Equal:
    case TokenKind_NotEqual:
    case TokenKind_Comma:
    case TokenKind_LParen:
    case TokenKind_RParen:
    case TokenKind_LBrace:
    case TokenKind_RBrace:
    case TokenKind_LBracket:
    case TokenKind_RBracket:
    case TokenKind_Function:
    case TokenKind_True:
    case TokenKind_False:
    case TokenKind_If:
    case TokenKind_Else:
    {
      stmt                  = arena_alloc_t(p->arena, AstStmt);
      stmt->tag             = AstStmt_Expr;
      stmt->data.expr.token = p->curr_token;
      stmt->data.expr.expr  = parse_expr(p, Precedence_Lowest);

      if (p->peek_token.kind == TokenKind_Semicolon)
      {
        parse_advance_token(p);
      }

      break;
    }

    case TokenKind_Let:
    {
      Token let_token   = p->curr_token;
      Token ident_token = p->peek_token;
      if (!parse_expect(p, TokenKind_Identifier)) break;
      if (!parse_expect(p, TokenKind_Assign)) break;

      parse_advance_token(p);

      stmt                          = arena_alloc_t(p->arena, AstStmt);
      AstExprIdentifier *identifier = arena_alloc_t(p->arena, AstExprIdentifier);
      AstExpr *expr                 = parse_expr(p, Precedence_Lowest);

      identifier->token = ident_token;

      stmt->tag                 = AstStmt_Let;
      stmt->data.let.token      = let_token;
      stmt->data.let.identifier = identifier;
      stmt->data.let.expr       = expr;

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

      stmt          = arena_alloc_t(p->arena, AstStmt);
      AstExpr *expr = parse_expr(p, Precedence_Lowest);

      stmt->tag            = AstStmt_Ret;
      stmt->data.ret.token = ret_token;
      stmt->data.ret.expr  = expr;

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
    default: assert(0 && "Tokenizer produced invalid token."); break;
  }

  return stmt;
}

// TODO(tad):
// Consider no branching expectation checks with result type, only breaking before allocating
// and initializing the statement. This might not be doable.
//
// Also consider usable sentinel types instead of null references. Could be useful for branchless
// expectation checks, but more generally as well.
internal Parser
parse(Str8 input)
{
  Parser p = { 0 };
  parse_init(&p, input, arena_create(KiB(4)));

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

#define OBJECT_KINDS(X) \
  X(Number)             \
  X(Boolean)            \
  X(Null)               \
  X(Return)             \
  X(Error)

typedef enum
{
#define OBJECT_KIND(name) ObjectKind_##name,
  OBJECT_KINDS(OBJECT_KIND)
#undef OBJECT_KIND
  ObjectKind_COUNT
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

typedef struct Object Object;

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
    ObjectReturn ret;
    ObjectNull null;
    ObjectError err;
  } data;
};

internal Object const *OBJECT_TRUE  = &(Object){ ObjectKind_Boolean, { .boolean.value = true } };
internal Object const *OBJECT_FALSE = &(Object){ ObjectKind_Boolean, { .boolean.value = false } };
internal Object const *OBJECT_NULL  = &(Object){ ObjectKind_Null, { 0 } };

internal Str8
eval_inspect(Object const *o, Arena *arena)
{
  switch (o->tag)
  {
    case ObjectKind_Number: return str8_f(arena, "%lld", o->data.number.value);
    case ObjectKind_Boolean: return o->data.boolean.value ? KEYWORD_TRUE : KEYWORD_FALSE;
    case ObjectKind_Return: return eval_inspect(o->data.ret.value, arena);
    case ObjectKind_Null: return str8_lit("null");
    case ObjectKind_Error: return str8_f(arena, "Error: %.*s", str8_va(o->data.err.value));
    default:
      return str8_f(arena, "unsupported object inspection: %.*s", str8_va(object_name(o->tag)));
  }
}

internal Object const *eval_program(AstStmt *statements, Arena *arena);
internal Object const *eval_block(AstStmtBlock *block, Arena *arena);
internal Object const *eval_expr(AstExpr *expr, Arena *arena);
internal Object const *eval_expr_prefix(AstExprPrefix prefix, Arena *arena);
internal Object const *eval_expr_infix(AstExprInfix infix, Arena *arena);
internal Object const *eval_expr_if_else(AstExprIfElse if_else, Arena *arena);

internal Object const *
object_from_number(s64 value, Arena *arena)
{
  Object *result            = arena_alloc_t(arena, Object);
  result->tag               = ObjectKind_Number;
  result->data.number.value = value;
  return result;
}

internal Object const *
object_from_error(Str8 error, Arena *arena)
{
  Object *result         = arena_alloc_t(arena, Object);
  result->tag            = ObjectKind_Error;
  result->data.err.value = error;
  return result;
}

internal Object const *
object_from_bool(b8 value)
{
  return value ? OBJECT_TRUE : OBJECT_FALSE;
}

internal b8
object_is_error(Object const *object)
{
  return object->tag == ObjectKind_Error;
}

internal b8
object_is_truthy(Object const *object)
{
  return object != OBJECT_NULL && object != OBJECT_FALSE;
}

internal Object const *
eval_expr_prefix(AstExprPrefix prefix, Arena *arena)
{
  Object const *result;

  switch (prefix.token.kind)
  {
    case TokenKind_Bang:
    {
      TmpArena scratch  = scratch_begin(arena);
      Object const *rhs = eval_expr(prefix.rhs, scratch.arena);
      if (object_is_error(rhs))
      {
        result = object_from_error(rhs->data.err.value, arena);
      }
      else
      {
        result = object_from_bool(!object_is_truthy(rhs));
      }
      scratch_end(scratch);
      break;
    }

    case TokenKind_Minus:
    {
      TmpArena scratch  = scratch_begin(arena);
      Object const *rhs = eval_expr(prefix.rhs, scratch.arena);
      if (object_is_error(rhs))
      {
        result = object_from_error(rhs->data.err.value, arena);
      }
      else if (rhs->tag != ObjectKind_Number)
      {
        Str8 error = str8_f(arena, "unknown operator: -%.*s", str8_va(object_name(rhs->tag)));
        result     = object_from_error(error, arena);
      }
      else
      {
        result = object_from_number(-rhs->data.number.value, arena);
      }
      scratch_end(scratch);
      break;
    }

    default:
    {
      Str8 error = str8_f(arena,
                          "unknown operator: %.*s%.*s",
                          str8_va(prefix.token.value),
                          str8_va(ast_expr_name(prefix.rhs->tag)));
      result     = object_from_error(error, arena);
    }
  }

  return result;
}

internal Object const *
eval_expr_infix(AstExprInfix infix, Arena *arena)
{
  TmpArena scratch  = scratch_begin(arena);
  Object const *lhs = eval_expr(infix.lhs, scratch.arena);
  Object const *rhs = eval_expr(infix.rhs, scratch.arena);

  Object const *result;

  if (object_is_error(lhs))
  {
    result = object_from_error(lhs->data.err.value, arena);
  }
  else if (object_is_error(rhs))
  {
    result = object_from_error(rhs->data.err.value, arena);
  }
  else if (lhs->tag == ObjectKind_Number && rhs->tag == ObjectKind_Number)
  {
    switch (infix.token.kind)
    {
      case TokenKind_Plus:
        result = object_from_number(lhs->data.number.value + rhs->data.number.value, arena);
        break;
      case TokenKind_Minus:
        result = object_from_number(lhs->data.number.value - rhs->data.number.value, arena);
        break;
      case TokenKind_Star:
        result = object_from_number(lhs->data.number.value * rhs->data.number.value, arena);
        break;
      case TokenKind_Slash:
        result = object_from_number(lhs->data.number.value / rhs->data.number.value, arena);
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
      default:
      {
        Str8 error = str8_f(arena,
                            "unknown operator: %.*s %.*s %.*s",
                            str8_va(object_name(lhs->tag)),
                            str8_va(infix.token.value),
                            str8_va(object_name(rhs->tag)));
        result     = object_from_error(error, arena);
        break;
      }
    }
  }
  else
  {
    switch (infix.token.kind)
    {
      case TokenKind_Equal: result = object_from_bool(lhs == rhs); break;
      case TokenKind_NotEqual: result = object_from_bool(lhs != rhs); break;
      default:
      {
        Str8 error;
        if (lhs->tag == rhs->tag)
        {
          error = str8_f(arena,
                         "unknown operator: %.*s %.*s %.*s",
                         str8_va(object_name(lhs->tag)),
                         str8_va(infix.token.value),
                         str8_va(object_name(rhs->tag)));
        }
        else
        {
          error = str8_f(arena,
                         "type mismatch: %.*s %.*s %.*s",
                         str8_va(object_name(lhs->tag)),
                         str8_va(infix.token.value),
                         str8_va(object_name(rhs->tag)));
        }
        result = object_from_error(error, arena);
        break;
      }
    }
  }

  scratch_end(scratch);

  return result;
}

internal Object const *
eval_expr_if_else(AstExprIfElse if_else, Arena *arena)
{
  TmpArena tmp            = arena_tmp_begin(arena);
  Object const *condition = eval_expr(if_else.condition, tmp.arena);
  if (object_is_error(condition)) return condition;

  b8 is_true = object_is_truthy(condition);
  arena_tmp_end(tmp);

  if (is_true)
  {
    return eval_block(if_else.consequence, arena);
  }
  else if (if_else.alternative != 0)
  {
    return eval_block(if_else.alternative, arena);
  }
  else
  {
    return OBJECT_NULL;
  }
}

internal Object const *
eval_expr(AstExpr *expr, Arena *arena)
{
  switch (expr->tag)
  {
    case AstExpr_Identifier: return OBJECT_NULL;

    case AstExpr_Number: return object_from_number(expr->data.number.value, arena);
    case AstExpr_Boolean: return object_from_bool(expr->data.boolean.value);
    case AstExpr_Prefix: return eval_expr_prefix(expr->data.prefix, arena);
    case AstExpr_Infix: return eval_expr_infix(expr->data.infix, arena);
    case AstExpr_IfElse: return eval_expr_if_else(expr->data.if_else, arena);

    case AstExpr_Function: return OBJECT_NULL;
    case AstExpr_Call: return OBJECT_NULL;

    default: return OBJECT_NULL;
  }
}

internal Object const *
eval_stmt(AstStmt *stmt, Arena *arena)
{
  switch (stmt->tag)
  {
    case AstStmt_Let: return OBJECT_NULL;
    case AstStmt_Ret:
    {
      Object const *ret_value = eval_expr(stmt->data.expr.expr, arena);
      if (object_is_error(ret_value)) return ret_value;

      Object *ret         = arena_alloc_t(arena, Object);
      ret->tag            = ObjectKind_Return;
      ret->data.ret.value = ret_value;
      return ret;
    }
    case AstStmt_Expr: return eval_expr(stmt->data.expr.expr, arena);

    default: return OBJECT_NULL;
  }
}

internal Object const *
eval_block(AstStmtBlock *block, Arena *arena)
{
  Object const *result = OBJECT_NULL;

  for (AstStmt *stmt = block->statements; stmt != 0; stmt = stmt->next)
  {
    result = eval_stmt(stmt, arena);

    if (result->tag == ObjectKind_Return) return result;
    if (result->tag == ObjectKind_Error) return result;
  }

  return result;
}

internal Object const *
eval_program(AstStmt *statements, Arena *arena)
{
  Object const *result = OBJECT_NULL;

  for (AstStmt *stmt = statements; stmt != 0; stmt = stmt->next)
  {
    result = eval_stmt(stmt, arena);

    if (result->tag == ObjectKind_Return) return result->data.ret.value;
    if (result->tag == ObjectKind_Error) return result;
  }

  return result;
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
  // TODO(tad): What to do with input?

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
  b32 print_prompt = true;

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
  b32 print_prompt = true;

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
    Parser p   = parse(input);

    parse_print_messages(&p);

    if (p.message_list.level < MessageLevel_Error)
    {
      printf("%.*s\n", str8_va(parse_to_string(&p)));
    }

    parse_free(&p);

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
  b32 print_prompt = true;

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
    Parser p   = parse(input);

    parse_print_messages(&p);

    if (p.message_list.level < MessageLevel_Error)
    {
      TmpArena scratch     = scratch_begin(0);
      Object const *result = eval_program(p.statements, p.arena);
      printf("%.*s\n", str8_va(eval_inspect(result, scratch.arena)));
      scratch_end(scratch);
    }

    parse_free(&p);

    print_prompt = input.buf[input.len - 1] == '\n';
  }
}

////////////////////////////////////////////////////////////////////////////////
// test

// TODO(tad): Printing is fine for this toy program, but it'd be more generally useful if these
// assertion macros collected failure messages for general use. This would also make it easier
// to build failure stack traces (currently handled by printing).

#define test_fail(fmt, ...) test_assert_m(false, fmt, ##__VA_ARGS__)
#define test_assert(test)   test_assert_m(test, "test error")
#define test_assert_m(test, fmt, ...)                                                           \
  do                                                                                            \
  {                                                                                             \
    if (!(test))                                                                                \
    {                                                                                           \
      printf("\033[0;31m(%s:%d) failure: " fmt "\033[0m\n", __func__, __LINE__, ##__VA_ARGS__); \
      return 1;                                                                                 \
    }                                                                                           \
  } while (0)
#define test_helper(help)                                          \
  do                                                               \
  {                                                                \
    if (help)                                                      \
    {                                                              \
      printf("\033[0;31m-> (%s:%d)\033[0m\n", __func__, __LINE__); \
      return 1;                                                    \
    }                                                              \
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
                        "@\n"
                        "");

#define test_result(kind, literal) { kind, str8_lit(literal) }
  Token test_results[] = {
    test_result(TokenKind_Let, "let"),        test_result(TokenKind_Identifier, "five"),
    test_result(TokenKind_Assign, "="),       test_result(TokenKind_Number, "5"),
    test_result(TokenKind_Semicolon, ";"),    test_result(TokenKind_Let, "let"),
    test_result(TokenKind_Identifier, "ten"), test_result(TokenKind_Assign, "="),
    test_result(TokenKind_Number, "10"),      test_result(TokenKind_Semicolon, ";"),

    test_result(TokenKind_Let, "let"),        test_result(TokenKind_Identifier, "add"),
    test_result(TokenKind_Assign, "="),       test_result(TokenKind_Function, "fn"),
    test_result(TokenKind_LParen, "("),       test_result(TokenKind_Identifier, "x"),
    test_result(TokenKind_Comma, ","),        test_result(TokenKind_Identifier, "y"),
    test_result(TokenKind_RParen, ")"),       test_result(TokenKind_LBrace, "{"),
    test_result(TokenKind_Identifier, "x"),   test_result(TokenKind_Plus, "+"),
    test_result(TokenKind_Identifier, "y"),   test_result(TokenKind_Semicolon, ";"),
    test_result(TokenKind_RBrace, "}"),       test_result(TokenKind_Semicolon, ";"),

    test_result(TokenKind_Let, "let"),        test_result(TokenKind_Identifier, "result"),
    test_result(TokenKind_Assign, "="),       test_result(TokenKind_Identifier, "add"),
    test_result(TokenKind_LParen, "("),       test_result(TokenKind_Identifier, "five"),
    test_result(TokenKind_Comma, ","),        test_result(TokenKind_Identifier, "ten"),
    test_result(TokenKind_RParen, ")"),       test_result(TokenKind_Semicolon, ";"),

    test_result(TokenKind_Bang, "!"),         test_result(TokenKind_Minus, "-"),
    test_result(TokenKind_Slash, "/"),        test_result(TokenKind_Star, "*"),
    test_result(TokenKind_Number, "5"),       test_result(TokenKind_Semicolon, ";"),
    test_result(TokenKind_Number, "5"),       test_result(TokenKind_Less, "<"),
    test_result(TokenKind_Number, "10"),      test_result(TokenKind_Greater, ">"),
    test_result(TokenKind_Number, "5"),       test_result(TokenKind_If, "if"),
    test_result(TokenKind_LParen, "("),       test_result(TokenKind_Number, "5"),
    test_result(TokenKind_Less, "<"),         test_result(TokenKind_Number, "10"),
    test_result(TokenKind_RParen, ")"),       test_result(TokenKind_LBrace, "{"),
    test_result(TokenKind_Return, "return"),  test_result(TokenKind_True, "true"),
    test_result(TokenKind_Semicolon, ";"),    test_result(TokenKind_RBrace, "}"),
    test_result(TokenKind_Else, "else"),      test_result(TokenKind_LBrace, "{"),
    test_result(TokenKind_Return, "return"),  test_result(TokenKind_False, "false"),
    test_result(TokenKind_Semicolon, ";"),    test_result(TokenKind_RBrace, "}"),

    test_result(TokenKind_Number, "10"),      test_result(TokenKind_Equal, "=="),
    test_result(TokenKind_Number, "10"),      test_result(TokenKind_Number, "10"),
    test_result(TokenKind_NotEqual, "!="),    test_result(TokenKind_Number, "9"),
    test_result(TokenKind_Illegal, "@"),
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
    Parser p = parse(tests[i].input);
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

    parse_free(&p);
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
    Parser p = parse(tests[i].input);
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected return statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Ret, "expected return statement");
    test_helper(test_lit_expr(stmt->data.ret.expr, tests[i].expected));

    parse_free(&p);
  }

  return 0;
}

internal int
test_parse_stmt_expr(void)
{
  Str8 input                  = str8_lit("test_identifier;");
  Str8 expected_identifiers[] = { str8_lit("test_identifier") };

  Parser p = parse(input);
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

  parse_free(&p);

  return 0;
}

internal int
test_parse_expr_identifier(void)
{
  Str8 input                  = str8_lit("test_identifier; test_identifier2");
  Str8 expected_identifiers[] = { str8_lit("test_identifier"), str8_lit("test_identifier2") };

  Parser p = parse(input);
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

  parse_free(&p);

  return 0;
}

internal int
test_parse_expr_number(void)
{
  Str8 input             = str8_lit("42069; 32");
  s64 expected_numbers[] = { 42069, 32 };

  Parser p = parse(input);
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

  parse_free(&p);

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
    Parser p = parse(tests[i].input);
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected prefix statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");
    test_helper(test_prefix_lit_expr(stmt->data.expr.expr, tests[i].op, tests[i].expected));

    parse_free(&p);
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
    Parser p = parse(tests[i].input);
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected infix statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");
    test_helper(test_infix_lit_expr(stmt->data.expr.expr, tests[i].op, tests[i].lhs, tests[i].rhs));

    parse_free(&p);
  }

  return 0;
}

internal int
test_parse_expr_if(void)
{
  Str8 input = str8_lit("if (x < y) { x }");

  Parser p = parse(input);
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

  parse_free(&p);

  return 0;
}

internal int
test_parse_expr_if_else(void)
{
  Str8 input = str8_lit("if (x < y) { x } else { y }");

  Parser p = parse(input);
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

  parse_free(&p);

  return 0;
}

internal int
test_parse_expr_function_lit(void)
{
  Str8 input = str8_lit("fn(x, y) { x + y; }");

  Parser p = parse(input);
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

  parse_free(&p);

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
    Parser p = parse(tests[i].input);
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

    parse_free(&p);
  }

  return 0;
}

internal int
test_parse_expr_call(void)
{
  Str8 input = str8_lit("add(1, 2 * 3, val);");

  Parser p = parse(input);
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

  parse_free(&p);

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
    Parser p = parse(tests[i].input);
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

    parse_free(&p);
  }

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
      str8_lit("add(a,b,1,(2 * 3),(4 + 5),add(6,(7 * 8)))"),
    },
    {
      str8_lit("add(a + b + c * d / f + g)"),
      str8_lit("add((((a + b) + ((c * d) / f)) + g))"),
    },
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    Parser p = parse(tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Str8 actual = parse_to_string(&p);

    test_assert_m(str8_equal(tests[i].expected, actual),
                  "expected precedence of '%.*s', parsed '%.*s'",
                  str8_va(tests[i].expected),
                  str8_va(actual));

    parse_free(&p);
  }

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
    Parser p = parse(tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Object const *result = eval_program(p.statements, p.arena);
    test_assert_m(result->tag == ObjectKind_Number, "expected number object");
    test_assert_m(result->data.number.value == tests[i].expected,
                  "expected number value '%lld', evaluated '%lld'",
                  tests[i].expected,
                  result->data.number.value);

    parse_free(&p);
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
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    Parser p = parse(tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Object const *result = eval_program(p.statements, p.arena);
    test_assert_m(result->tag == ObjectKind_Boolean, "expected boolean object");
    test_assert_m(result->data.boolean.value == tests[i].expected,
                  "expected boolean value '%d', evaluated '%d'",
                  tests[i].expected,
                  result->data.boolean.value);

    parse_free(&p);
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
    Parser p = parse(tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Object const *result = eval_program(p.statements, p.arena);
    test_assert_m(result->tag == ObjectKind_Boolean, "expected boolean object");
    test_assert_m(result->data.boolean.value == tests[i].expected,
                  "expected boolean value '%d', evaluated '%d'",
                  tests[i].expected,
                  result->data.boolean.value);

    parse_free(&p);
  }

  return 0;
}

internal int
test_eval_if_else_expr(void)
{
  struct
  {
    Str8 input;
    b8 is_null;
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
    Parser p = parse(tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Object const *result = eval_program(p.statements, p.arena);
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

    parse_free(&p);
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
    Parser p = parse(tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Object const *result = eval_program(p.statements, p.arena);
    test_assert_m(result->tag == ObjectKind_Number, "expected number object");
    test_assert_m(result->data.number.value == tests[i].expected,
                  "expected number value '%lld', evaluated '%lld'",
                  tests[i].expected,
                  result->data.number.value);

    parse_free(&p);
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
  };

  for (usize i = 0; i < arr_count(tests); i++)
  {
    Parser p = parse(tests[i].input);
    test_helper(test_parse_check_messages(&p));

    Object const *result = eval_program(p.statements, p.arena);
    test_assert_m(result->tag == ObjectKind_Error, "expected error object");
    test_assert_m(str8_equal(tests[i].expected, result->data.err.value),
                  "expected error '%.*s', evaluated '%.*s'",
                  str8_va(tests[i].expected),
                  str8_va(result->data.err.value));

    parse_free(&p);
  }

  return 0;
}

internal int
test(void)
{
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

  result += test_parse_op_precedence();

  result += test_eval_number_expr();
  result += test_eval_boolean_expr();
  result += test_eval_bang();
  result += test_eval_if_else_expr();

  result += test_eval_return_stmt();

  result += test_eval_error_handling();

  // TODO(tad): Investigate segfault when eval "1 == (return 1)"

  return result;
}
