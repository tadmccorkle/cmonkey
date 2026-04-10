#include <assert.h>
#include <ctype.h>
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

internal per_thread Arena *tl_arena;

internal Arena *
arena_tl_get(void)
{
  if (!tl_arena) tl_arena = arena_create(ARENA_DEFAULT_SIZE);
  return tl_arena;
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
  return (p - c);
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

internal Str8
str8_fv(Arena *arena, char const *fmt, va_list args)
{
  va_list args2;
  va_copy(args2, args);
  usize len = vsnprintf(0, 0, fmt, args);
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
  return str8((u8 *)c, cstr_length(c));
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

internal void
str8_append_fv(StrBuilder8 *builder, char const *fmt, va_list args)
{
  va_list args2;
  va_copy(args2, args);
  usize append_len = vsnprintf(0, 0, fmt, args);
  str8_ensure_capacity(builder, builder->len + append_len);
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
  X(TokenKind_Illegal)         \
  X(TokenKind_EOF)             \
                               \
  /* identifiers & literals */ \
  X(TokenKind_Identifier)      \
  X(TokenKind_Number)          \
                               \
  /* operators */              \
  X(TokenKind_Assign)          \
  X(TokenKind_Plus)            \
  X(TokenKind_Minus)           \
  X(TokenKind_Bang)            \
  X(TokenKind_Star)            \
  X(TokenKind_Slash)           \
  X(TokenKind_Less)            \
  X(TokenKind_Greater)         \
  X(TokenKind_Equal)           \
  X(TokenKind_NotEqual)        \
                               \
  /* delimiters */             \
  X(TokenKind_Comma)           \
  X(TokenKind_Semicolon)       \
  X(TokenKind_LParen)          \
  X(TokenKind_RParen)          \
  X(TokenKind_LBrace)          \
  X(TokenKind_RBrace)          \
  X(TokenKind_LBracket)        \
  X(TokenKind_RBracket)        \
                               \
  /* keywords */               \
  X(TokenKind_Function)        \
  X(TokenKind_Let)             \
  X(TokenKind_True)            \
  X(TokenKind_False)           \
  X(TokenKind_If)              \
  X(TokenKind_Else)            \
  X(TokenKind_Return)

typedef enum
{
#define TOKEN_KIND(name) name,
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
  case name: return str8_lit(#name);
    TOKEN_KINDS(TOKEN_KIND)
#undef TOKEN_KIND
    default: return str8_lit("Unknown Token Kind");
  }
}

typedef struct Token Token;
struct Token
{
  Str8 value;
  TokenKind kind;
};

internal const Str8 KEYWORD_FUNCTION = str8_lit("fn");
internal const Str8 KEYWORD_LET      = str8_lit("let");
internal const Str8 KEYWORD_TRUE     = str8_lit("true");
internal const Str8 KEYWORD_FALSE    = str8_lit("false");
internal const Str8 KEYWORD_IF       = str8_lit("if");
internal const Str8 KEYWORD_ELSE     = str8_lit("else");
internal const Str8 KEYWORD_RETURN   = str8_lit("return");

internal Token keyword_tokens[] = {
  { KEYWORD_FUNCTION, TokenKind_Function },
  { KEYWORD_LET, TokenKind_Let },
  { KEYWORD_TRUE, TokenKind_True },
  { KEYWORD_FALSE, TokenKind_False },
  { KEYWORD_IF, TokenKind_If },
  { KEYWORD_ELSE, TokenKind_Else },
  { KEYWORD_RETURN, TokenKind_Return },
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
      usize length = 0;
      usize pos    = l->pos;

      if (is_digit(ch))
      {
        do length += 1;
        while (pos < l->input.len && is_digit(l->input.buf[++pos]));

        lex_init_token(l, token, length, TokenKind_Number);
      }
      else if (ch == '_' || is_letter(ch))
      {
        do
        {
          length += 1;
          ch = l->input.buf[++pos];
        } while (pos < l->input.len && (ch == '_' || is_letter(ch) || is_digit(ch)));

        TokenKind kind = TokenKind_Identifier;
        for (usize i = 0; i < arr_count(keyword_tokens); i++)
        {
          Str8 keyword = keyword_tokens[i].value;

          if (keyword.len != length) continue;

          if (!memcmp(keyword.buf, &l->input.buf[l->pos], keyword.len))
          {
            kind = keyword_tokens[i].kind;
          }
        }

        lex_init_token(l, token, length, kind);
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

  printf("id %-30s %s\n", "token", "value");
  do
  {
    lex_advance_token(&l, &token);
    printf("%2d %-30.*s %.*s\n", token.kind, str8_va(token_name(token.kind)), str8_va(token.value));
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

// TODO(tad): Build expression and statement kinds with macro to support forward declarations,
// pretty-printing, etc.

typedef enum
{
  AstExpr_Identifier,
  AstExpr_Number,
  AstExpr_Boolean,
  AstExpr_Prefix,
  AstExpr_Infix,
  AstExpr_IfElse,
  AstExpr_Function,
  AstExpr_Call,
  AstExpr_COUNT,
} AstExprKind;

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

typedef struct AstExprIf AstExprIf;
struct AstExprIf
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
    AstExprIf if_else;
    AstExprFunction function;
    AstExprCall call;
  } data;
};

typedef enum
{
  AstStmt_Let,
  AstStmt_Ret,
  AstStmt_Expr,
  AstStmt_COUNT,
} AstStmtKind;

struct AstStmt
{
  AstStmt *next;

  AstStmtKind tag;
  union
  {
    struct Let
    {
      Token token;
      AstExprIdentifier *identifier;
      AstExpr *expr;
    } let;
    struct Return
    {
      Token token;
      AstExpr *expr;
    } ret;
    struct Expr
    {
      Token token;
      AstExpr *expr;
    } expr;
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
    // TODO(tad): print error level
    printf("%.*s\n", str8_va(m->value));
  }
}

internal void
parse_error(Parser *p, MessageLevel level, char const *fmt, ...)
{
  Message *m = arena_alloc_t(p->arena, Message);
  m->level   = level;

  va_list args;
  va_start(args, fmt);
  m->value = str8_fv(p->arena, fmt, args);
  va_end(args);

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
    parse_error(p,
                MessageLevel_Error,
                "Expected token '%.*s' but parsed '%.*s'.",
                str8_va(token_name(kind)),
                str8_va(token_name(p->peek_token.kind)));
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

// TODO(tad): Refactor these to make use of new TmpArena commit function. Can rollback invalid
// allocations instead of leaving in arena.
internal AstStmt *parse_stmt(Parser *p);
internal AstExpr *parse_expr(Parser *p, Precedence precedence);
internal AstStmtBlock *parse_block(Parser *p);

internal AstStmtBlock *
parse_block(Parser *p)
{
  AstStmtBlock *block = arena_alloc_t(p->arena, AstStmtBlock);
  block->token        = p->curr_token;

  parse_advance_token(p);

  AstStmt sentinel = { 0 };
  AstStmt *tail    = &sentinel;

  // TODO(tad): Error here if EOF is reached instead of RBrace?
  while (p->curr_token.kind != TokenKind_RBrace && p->curr_token.kind != TokenKind_EOF)
  {
    AstStmt *stmt = parse_stmt(p);
    if (stmt)
    {
      tail->next = stmt;
      tail       = stmt;
    }

    parse_advance_token(p);
  }

  block->statements = sentinel.next;

  return block;
}

internal AstExpr *
parse_expr(Parser *p, Precedence precedence)
{
  AstExpr *lhs = 0;

  // prefix
  {
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
        lhs                    = arena_alloc_t(p->arena, AstExpr);
        lhs->tag               = AstExpr_Number;
        lhs->data.number.token = p->curr_token;
        // TODO(tad): handle error here, could use tmp buf to ensure null termination
        lhs->data.number.value = strtoll((char *)p->curr_token.value.buf, 0, 10);
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
        lhs                    = arena_alloc_t(p->arena, AstExpr);
        lhs->tag               = AstExpr_Prefix;
        lhs->data.prefix.token = p->curr_token;
        parse_advance_token(p);
        lhs->data.prefix.rhs = parse_expr(p, Precedence_Prefix);
        break;
      }

      case TokenKind_If:
      {
        AstExpr *if_expr;
        Token if_token = p->curr_token;

        if (!parse_expect(p, TokenKind_LParen)) break;
        if_expr = arena_alloc_t(p->arena, AstExpr);
        parse_advance_token(p);
        AstExpr *condition = parse_expr(p, Precedence_Lowest);
        if (condition == 0) break;
        if (!parse_expect(p, TokenKind_RParen)) break;

        if (!parse_expect(p, TokenKind_LBrace)) break;
        AstStmtBlock *consequence = parse_block(p);

        AstStmtBlock *alternative = 0;
        if (p->peek_token.kind == TokenKind_Else)
        {
          parse_advance_token(p);
          if (!parse_expect(p, TokenKind_LBrace)) break;
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
        AstExpr *fn_expr;
        Token fn_token = p->curr_token;

        if (!parse_expect(p, TokenKind_LParen)) break;

        fn_expr = arena_alloc_t(p->arena, AstExpr);

        AstExprFunctionParam params_sentinel = { 0 };
        AstExprFunctionParam *params_tail    = &params_sentinel;

        if (p->peek_token.kind != TokenKind_RParen)
        {
          while (parse_expect(p, TokenKind_Identifier))
          {
            AstExprFunctionParam *param = arena_alloc_t(p->arena, AstExprFunctionParam);
            param->identifier.token     = p->curr_token;

            params_tail->next = param;
            params_tail       = param;

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

        if (!parse_expect(p, TokenKind_LBrace)) break;
        AstStmtBlock *body = parse_block(p);

        lhs                       = fn_expr;
        lhs->tag                  = AstExpr_Function;
        lhs->data.function.token  = fn_token;
        lhs->data.function.params = params_sentinel.next;
        lhs->data.function.body   = body;
        break;
      }

      case TokenKind_LParen:
      {
        parse_advance_token(p);

        AstExpr *grouped_expr = parse_expr(p, Precedence_Lowest);
        if (parse_expect(p, TokenKind_RParen))
        {
          lhs = grouped_expr;
        }
        break;
      }

      default:
        parse_error(p,
                    MessageLevel_Error,
                    "'%.*s' (type '%.*s') is not a valid prefix token.",
                    str8_va(p->curr_token.value),
                    str8_va(token_name(p->curr_token.kind)));
        return lhs;
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

        AstExprCallArg args_sentinel = { 0 };
        AstExprCallArg *args_tail    = &args_sentinel;

        if (p->peek_token.kind != TokenKind_RParen)
        {
          for (;;)
          {
            parse_advance_token(p);

            AstExprCallArg *arg = arena_alloc_t(p->arena, AstExprCallArg);
            arg->expr           = parse_expr(p, Precedence_Lowest);

            args_tail->next = arg;
            args_tail       = arg;

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
        call->data.call.args     = args_sentinel.next;

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
    // TODO(tad): some kinds are not appropriate here (e.g., semicolon)
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

      if (p->curr_token.kind != TokenKind_Semicolon)
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

      if (p->curr_token.kind != TokenKind_Semicolon)
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
        u8 c      = p->curr_token.value.buf[i];
        char *fmt = c >= 32 && c <= 126 ? "Illegal token: %c" : "Illegal unprintable token: \\x%X";
        parse_error(p, MessageLevel_Error, fmt, c);
      }
      break;
    }
    case TokenKind_EOF: assert(!"EOF tokens are checked by outer loop condition."); break;
    default: assert(!"Tokenizer produced invalid token."); break;
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

  AstStmt sentinel = { 0 };
  AstStmt *tail    = &sentinel;

  while (p.curr_token.kind != TokenKind_EOF)
  {
    AstStmt *stmt = parse_stmt(&p);
    if (stmt)
    {
      tail->next = stmt;
      tail       = stmt;
    }

    parse_advance_token(&p);
  }

  p.statements = sentinel.next;

  return p;
}

////////////////////////////////////////////////////////////////////////////////
// eval

typedef enum
{
  ObjectKind_Number,
  ObjectKind_Boolean,
  ObjectKind_Null,
  ObjectKind_COUNT
} ObjectKind;

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
};

typedef struct Object Object;
struct Object
{
  ObjectKind tag;
  union
  {
    ObjectNumber number;
    ObjectBoolean boolean;
    ObjectNull null;
  } data;
};

internal Object const *OBJECT_TRUE  = &(Object){ ObjectKind_Boolean, { .boolean.value = true } };
internal Object const *OBJECT_FALSE = &(Object){ ObjectKind_Boolean, { .boolean.value = false } };
internal Object const *OBJECT_NULL  = &(Object){ ObjectKind_Null, { 0 } };

internal Str8
eval_inspect(Object const *o, Arena *arena)
{
  Str8 result = { 0 };
  switch (o->tag)
  {
    case ObjectKind_Number: result = str8_f(arena, "%lld", o->data.number.value); break;
    case ObjectKind_Boolean: result = o->data.boolean.value ? KEYWORD_TRUE : KEYWORD_FALSE; break;
    case ObjectKind_Null: result = str8_lit("null"); break;
    default:
      // TODO(tad): error
      break;
  }

  return result;
}

internal Object const *
eval_expr(AstExpr *expr, Arena *arena)
{
  switch (expr->tag)
  {
    case AstExpr_Identifier: break;
    case AstExpr_Number:
    {
      Object *result            = arena_alloc_t(arena, Object);
      result->tag               = ObjectKind_Number;
      result->data.number.value = expr->data.number.value;
      return result;
    }
    case AstExpr_Boolean:
    {
      return expr->data.boolean.value ? OBJECT_TRUE : OBJECT_FALSE;
      break;
    }
    case AstExpr_Prefix: break;
    case AstExpr_Infix: break;
    case AstExpr_IfElse: break;
    case AstExpr_Function: break;
    case AstExpr_Call: break;
    default:
      // TODO(tad): error
      break;
  }

  return OBJECT_NULL;
}

internal Object const *
eval_stmt(AstStmt *statements, Arena *arena)
{
  Object const *result;
  for (AstStmt *stmt = statements; stmt != 0; stmt = stmt->next)
  {
    switch (stmt->tag)
    {
      case AstStmt_Let: break;
      case AstStmt_Ret: break;
      case AstStmt_Expr: result = eval_expr(stmt->data.expr.expr, arena); break;
      default: break;
    }
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
  Str8 input = { 0 };
  if (argc > 1)
  {
    if (strcmp(argv[1], "test") == 0)
    {
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

    if (strcmp(argv[1], "int") == 0)
    {
      return repl();
    }

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

    printf("tokenized '%s':\n\n", argv[1]);
  }
  else
  {
    input = str8_lit("let five = 5;\n"
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

    printf("test input:\n\n%.*s", str8_va(input));
    printf("\n---------------\n\n");
    printf("tokenized test input:\n\n");
  }

  lex_print(input);

  Parser p            = parse(input);
  Str8 parsed_program = parse_to_string(&p);
  printf("\nParsed program:\n\n");
  printf("%.*s\n", str8_va(parsed_program));
  if (p.message_list.count > 0)
  {
    printf("\n---------------\n\n");
    parse_print_messages(&p);
  }
  parse_free(&p);

  return 0;
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
      Arena *a             = arena_tl_get();
      TmpArena t           = arena_tmp_begin(a);
      Object const *result = eval_stmt(p.statements, p.arena);
      printf("%.*s\n", str8_va(eval_inspect(result, a)));
      arena_tmp_end(t);
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

#define test_result(kind, literal) { str8_lit(literal), kind }
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
    { str8_lit("fn(){}"), 0, {} },
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
    { str8_lit("call();"), 0, {} },
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

  return result;
}
