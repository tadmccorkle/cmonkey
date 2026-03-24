#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
// base

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
str8_from_cstr(char const *c)
{
  return str8((u8 *)c, cstr_length(c));
}

internal b32
str8_equal(Str8 a, Str8 b)
{
  return a.len == b.len && memcmp(a.buf, b.buf, a.len) == 0;
}

////////////////////////////////////////////////////////////////////////////////
// arena

#define ARENA_DEFAULT_SIZE MiB(2)

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

typedef struct TempArena TmpArena;
struct TempArena
{
  Arena *arena;
  ArenaBlock *block;
  usize pos;
};

internal ArenaBlock *
arena_block_alloc(usize size)
{
  ArenaBlock *block = malloc(sizeof(ArenaBlock) + size);
  block->prev       = 0;
  block->size       = size;
  block->pos        = 0;
  return block;
}

internal Arena *
arena_alloc(usize size)
{
  Arena *arena   = malloc(sizeof(Arena));
  arena->current = arena_block_alloc(size);
  return arena;
}

internal void *
arena_push(Arena *arena, usize size, usize align)
{
  ArenaBlock *block = arena->current;
  usize pos         = align_up(block->pos, align);

  if (pos + size > block->size)
  {
    usize new_size = block->size * 2;
    if (size > new_size)
    {
      new_size = size + align;
    }

    ArenaBlock *new_block = arena_block_alloc(new_size);
    new_block->prev       = block;
    arena->current        = new_block;
    block                 = new_block;
    pos                   = 0;
  }

  void *result = block->data + pos;
  block->pos   = pos + size;
  memset(result, 0, size);
  return result;
}

#define arena_push_t(arena, T) (T *)arena_push(arena, sizeof(T), _Alignof(T));

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
  if (!tl_arena) tl_arena = arena_alloc(ARENA_DEFAULT_SIZE);
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
arena_tmp_end(TmpArena tmp)
{
  Arena *arena = tmp.arena;

  while (arena->current != tmp.block)
  {
    ArenaBlock *block = arena->current;
    arena->current    = block->prev;
    free(block);
  }

  arena->current->pos = tmp.pos;
}

////////////////////////////////////////////////////////////////////////////////
// lex

internal b32
is_whitespace(u8 ch)
{
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\v' || ch == '\f';
}

internal b32
all_whitespace(Str8 str)
{
  for (usize i = 0; i < str.len; i++)
  {
    if (!is_whitespace(str.buf[i]))
    {
      return 0;
    }
  }
  return 1;
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

internal const Token invalid_token = { .kind = TokenKind_Illegal };

internal Token keyword_tokens[] = {
  {     str8_cti("fn"), TokenKind_Function },
  {    str8_cti("let"),      TokenKind_Let },
  {   str8_cti("true"),     TokenKind_True },
  {  str8_cti("false"),    TokenKind_False },
  {     str8_cti("if"),       TokenKind_If },
  {   str8_cti("else"),     TokenKind_Else },
  { str8_cti("return"),   TokenKind_Return },
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

          if (keyword.len > length) continue;

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
  AstExpr_Identifier,
  AstExpr_Number,
  AstExpr_Prefix,
  AstExpr_Infix,
  // ...
  AstExpr_COUNT,
} AstExprKind;

typedef struct AstExpr AstExpr;

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

struct AstExpr
{
  AstExprKind tag;
  union
  {
    AstExprIdentifier identifier;
    AstExprNumber number;
    AstExprPrefix prefix;
    AstExprInfix infix;
    // ...
  } data;
};

typedef enum
{
  AstStmt_Let,
  AstStmt_Ret,
  AstStmt_Expr,
  AstStmt_COUNT,
} AstStmtKind;

typedef struct AstStmt AstStmt;
struct AstStmt
{
  AstStmtKind tag;
  AstStmt *next;
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

typedef enum
{
  Precedence_Lowest,
  Precedence_Equals,      // ==
  Precedence_LessGreater, // >,<
  Precedence_Sum,         // +
  Precedence_Product,     // *
  Precedence_Prefix,      // -,!
  Precedence_Call,        // func()
  Precedence_COUNT
} Precedence;

internal void
parse_init(Parser *p, Str8 input, Arena *arena)
{
  p->arena = arena;

  lex_init(&p->l, input);
  lex_advance_token(&p->l, &p->curr_token);
  lex_advance_token(&p->l, &p->peek_token);
}

internal void
parse_print_messages(Parser *p)
{
  for (Message *m = p->message_list.first; m != 0; m = m->next)
  {
    printf("%.*s\n", str8_va(m->value));
  }
}

internal void
parse_error(Parser *p, MessageLevel level, char const *fmt, ...)
{
  Message *m = arena_push_t(p->arena, Message);

  // TODO(tad): dedicated str8_f and str8_fv functions
  va_list args1;
  va_list args2;
  va_start(args1, fmt);
  va_copy(args2, args1);
  u32 len = vsnprintf(0, 0, fmt, args1);
  // TODO(tad): arena_push_t w/ count
  // TODO(tad): arena_push and etc. w/o zero mem
  char *buf = arena_push(p->arena, len + 1, _Alignof(u8));
  len       = vsnprintf(buf, len + 1, fmt, args2);
  buf[len]  = 0;
  va_end(args2);
  va_end(args1);

  m->level = level;
  m->value = str8((u8 *)buf, len);

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

internal void
parse_free(Parser *p)
{
  arena_free(p->arena);
}

// TODO(tad): consider printing complete AST and individual nodes for debugging/testing

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

    default: return Precedence_Lowest;
  }
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
        lhs                        = arena_push_t(p->arena, AstExpr);
        lhs->tag                   = AstExpr_Identifier;
        lhs->data.identifier.token = p->curr_token;
        break;
      }

      case TokenKind_Number:
      {
        lhs                    = arena_push_t(p->arena, AstExpr);
        lhs->tag               = AstExpr_Number;
        lhs->data.number.token = p->curr_token;
        lhs->data.number.value = strtoll((char *)p->curr_token.value.buf, 0, 10);
        break;
      }

      case TokenKind_Minus:
      case TokenKind_Bang:
      {
        lhs                    = arena_push_t(p->arena, AstExpr);
        lhs->tag               = AstExpr_Prefix;
        lhs->data.prefix.token = p->curr_token;
        parse_advance_token(p);
        lhs->data.prefix.rhs = parse_expr(p, Precedence_Prefix);
        break;
      }

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

        AstExpr *infix          = arena_push_t(p->arena, AstExpr);
        infix->tag              = AstExpr_Infix;
        infix->data.infix.token = p->curr_token;
        infix->data.infix.lhs   = lhs;

        Precedence infix_precedence = parse_get_precedence(p->curr_token.kind);
        parse_advance_token(p);
        infix->data.infix.rhs = parse_expr(p, infix_precedence);
        break;
      }

      default: return lhs;
    }
  }

  return lhs;
}

internal Parser
parse(Str8 input)
{
  Parser p = { 0 };
  parse_init(&p, input, arena_alloc(KiB(4)));

  AstStmt sentinel = { 0 };
  AstStmt *tail    = &sentinel;

  // TODO(tad): consider no branching expectation checks with result type,
  // only breaking before allocating and initializing the statement.

  while (p.curr_token.kind != TokenKind_EOF)
  {
    AstStmt *stmt = 0;

    switch (p.curr_token.kind)
    {
      case TokenKind_Let:
      {
        Token let_token   = p.curr_token;
        Token ident_token = p.peek_token;
        if (!parse_expect(&p, TokenKind_Identifier)) break;
        if (!parse_expect(&p, TokenKind_Assign)) break;

        // TODO(tad): parse expressions
        while (p.curr_token.kind != TokenKind_Semicolon)
        {
          parse_advance_token(&p);
        }

        stmt                          = arena_push_t(p.arena, AstStmt);
        AstExprIdentifier *identifier = arena_push_t(p.arena, AstExprIdentifier);
        // AstExpr *expr                 = arena_push_t(p.arena, AstExpr);

        identifier->token = ident_token;

        *stmt = (AstStmt){
          .tag = AstStmt_Let,
          .data.let = {
            .token = let_token,
            .identifier = identifier,
            // .expression = expr,
          },
        };

        break;
      }

      case TokenKind_Return:
      {
        Token ret_token = p.curr_token;

        // TODO(tad): parse expressions
        while (p.curr_token.kind != TokenKind_Semicolon)
        {
          parse_advance_token(&p);
        }

        stmt = arena_push_t(p.arena, AstStmt);
        // AstExpr *expr                 = arena_push_t(p.arena, AstExpr);

        *stmt = (AstStmt){
          .tag = AstStmt_Ret,
          .data.let = {
            .token = ret_token,
            // .expression = expr,
          },
        };
        break;
      }

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
      case TokenKind_Semicolon:
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
        stmt                  = arena_push_t(p.arena, AstStmt);
        stmt->tag             = AstStmt_Expr;
        stmt->data.expr.token = p.curr_token;
        stmt->data.expr.expr  = parse_expr(&p, Precedence_Lowest);

        if (p.peek_token.kind == TokenKind_Semicolon)
        {
          parse_advance_token(&p);
        }

        break;
      }

      case TokenKind_Illegal:
      {
        for (usize i = 0; i < p.curr_token.value.len; i++)
        {
          u8 c = p.curr_token.value.buf[i];
          char *fmt = c >= 32 && c <= 126 ? "Illegal token: %c" : "Illegal unprintable token: \\x%X";
          parse_error(&p, MessageLevel_Error, fmt, c);
        }
        break;
      }

      case TokenKind_EOF: assert(!"EOF tokens are checked by outer loop condition."); break;
      case TokenKind_COUNT:
      default: assert(!"Tokenizer produced invalid token."); break;
    }

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
// main

internal int test(void);
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

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
// test

#define test_assert_m(test, fmt, ...)                               \
  do                                                                \
  {                                                                 \
    if (!(test))                                                    \
    {                                                               \
      printf("\033[0;31mfailure: " fmt "\033[0m\n", ##__VA_ARGS__); \
      return 1;                                                     \
    }                                                               \
  } while (0)
#define test_assert(test) test_assert_m(test, "test error");

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

#define test_check_lex(kind, literal) { str8_cti(literal), kind }
  Token test_results[] = {
    test_check_lex(TokenKind_Let, "let"),        test_check_lex(TokenKind_Identifier, "five"),
    test_check_lex(TokenKind_Assign, "="),       test_check_lex(TokenKind_Number, "5"),
    test_check_lex(TokenKind_Semicolon, ";"),    test_check_lex(TokenKind_Let, "let"),
    test_check_lex(TokenKind_Identifier, "ten"), test_check_lex(TokenKind_Assign, "="),
    test_check_lex(TokenKind_Number, "10"),      test_check_lex(TokenKind_Semicolon, ";"),

    test_check_lex(TokenKind_Let, "let"),        test_check_lex(TokenKind_Identifier, "add"),
    test_check_lex(TokenKind_Assign, "="),       test_check_lex(TokenKind_Function, "fn"),
    test_check_lex(TokenKind_LParen, "("),       test_check_lex(TokenKind_Identifier, "x"),
    test_check_lex(TokenKind_Comma, ","),        test_check_lex(TokenKind_Identifier, "y"),
    test_check_lex(TokenKind_RParen, ")"),       test_check_lex(TokenKind_LBrace, "{"),
    test_check_lex(TokenKind_Identifier, "x"),   test_check_lex(TokenKind_Plus, "+"),
    test_check_lex(TokenKind_Identifier, "y"),   test_check_lex(TokenKind_Semicolon, ";"),
    test_check_lex(TokenKind_RBrace, "}"),       test_check_lex(TokenKind_Semicolon, ";"),

    test_check_lex(TokenKind_Let, "let"),        test_check_lex(TokenKind_Identifier, "result"),
    test_check_lex(TokenKind_Assign, "="),       test_check_lex(TokenKind_Identifier, "add"),
    test_check_lex(TokenKind_LParen, "("),       test_check_lex(TokenKind_Identifier, "five"),
    test_check_lex(TokenKind_Comma, ","),        test_check_lex(TokenKind_Identifier, "ten"),
    test_check_lex(TokenKind_RParen, ")"),       test_check_lex(TokenKind_Semicolon, ";"),

    test_check_lex(TokenKind_Bang, "!"),         test_check_lex(TokenKind_Minus, "-"),
    test_check_lex(TokenKind_Slash, "/"),        test_check_lex(TokenKind_Star, "*"),
    test_check_lex(TokenKind_Number, "5"),       test_check_lex(TokenKind_Semicolon, ";"),
    test_check_lex(TokenKind_Number, "5"),       test_check_lex(TokenKind_Less, "<"),
    test_check_lex(TokenKind_Number, "10"),      test_check_lex(TokenKind_Greater, ">"),
    test_check_lex(TokenKind_Number, "5"),       test_check_lex(TokenKind_If, "if"),
    test_check_lex(TokenKind_LParen, "("),       test_check_lex(TokenKind_Number, "5"),
    test_check_lex(TokenKind_Less, "<"),         test_check_lex(TokenKind_Number, "10"),
    test_check_lex(TokenKind_RParen, ")"),       test_check_lex(TokenKind_LBrace, "{"),
    test_check_lex(TokenKind_Return, "return"),  test_check_lex(TokenKind_True, "true"),
    test_check_lex(TokenKind_Semicolon, ";"),    test_check_lex(TokenKind_RBrace, "}"),
    test_check_lex(TokenKind_Else, "else"),      test_check_lex(TokenKind_LBrace, "{"),
    test_check_lex(TokenKind_Return, "return"),  test_check_lex(TokenKind_False, "false"),
    test_check_lex(TokenKind_Semicolon, ";"),    test_check_lex(TokenKind_RBrace, "}"),

    test_check_lex(TokenKind_Number, "10"),      test_check_lex(TokenKind_Equal, "=="),
    test_check_lex(TokenKind_Number, "10"),      test_check_lex(TokenKind_Number, "10"),
    test_check_lex(TokenKind_NotEqual, "!="),    test_check_lex(TokenKind_Number, "9"),
    test_check_lex(TokenKind_Illegal, "@"),
  };
#undef test_check_lex

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
test_parse_stmt_let(void)
{
  Str8 input = str8_lit("let x = 5; let y = 10;");

  Str8 expected_identifiers[] = {
    str8_cti("x"),
    str8_cti("y"),
  };

  Parser p = parse(input);
  parse_print_messages(&p);

  test_assert_m(p.message_list.level <= MessageLevel_Info, "unexpected parse errors");

  usize i = 0;
  for (AstStmt *stmt = p.statements; stmt != 0; stmt = stmt->next, i++)
  {
    test_assert_m(stmt->tag == AstStmt_Let, "expected let statement");
    test_assert_m(str8_equal(stmt->data.let.identifier->token.value, expected_identifiers[i]),
                  "unexpected let statement identifier");
  }

  test_assert_m(arr_count(expected_identifiers) == i,
                "expected %zu statements, parsed %zu",
                arr_count(expected_identifiers),
                i);

  parse_free(&p);

  return 0;
}

internal int
test_parse_stmt_ret(void)
{
  Str8 input                = str8_lit("return 5; return 10;");
  const u32 statement_count = 2;

  Parser p = parse(input);
  parse_print_messages(&p);

  test_assert_m(p.message_list.level <= MessageLevel_Info, "unexpected parse errors");

  usize i = 0;
  for (AstStmt *stmt = p.statements; stmt != 0; stmt = stmt->next, i++)
  {
    test_assert_m(stmt->tag == AstStmt_Ret, "expected return statement");
  }

  test_assert_m(statement_count == i, "expected %u statements, parsed %zu", statement_count, i);

  parse_free(&p);

  return 0;
}

internal int
test_parse_stmt_expr(void)
{
  Str8 input = str8_lit("test_identifier;");

  Str8 expected_identifiers[] = {
    str8_cti("test_identifier"),
  };

  Parser p = parse(input);
  parse_print_messages(&p);

  test_assert_m(p.message_list.level <= MessageLevel_Info, "unexpected parse errors");

  usize i = 0;
  for (AstStmt *stmt = p.statements; stmt != 0; stmt = stmt->next, i++)
  {
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");
    test_assert_m(stmt->data.expr.token.kind == TokenKind_Identifier, "expected identifier expression");
    test_assert_m(str8_equal(stmt->data.expr.token.value, expected_identifiers[i]),
                  "expected identifier expression '%.*s', parsed '%.*s'",
                  str8_va(expected_identifiers[i]),
                  str8_va(stmt->data.expr.token.value));
  }

  test_assert_m(arr_count(expected_identifiers) == i,
                "expected %zu statements, parsed %zu",
                arr_count(expected_identifiers),
                i);

  return 0;
}

internal int
test_parse_expr_identifier(void)
{
  Str8 input = str8_lit("test_identifier; other_test_identifier");

  Str8 expected_identifiers[] = {
    str8_cti("test_identifier"),
    str8_cti("other_test_identifier"),
  };

  Parser p = parse(input);
  parse_print_messages(&p);

  test_assert_m(p.message_list.level <= MessageLevel_Info, "unexpected parse errors");

  usize i = 0;
  for (AstStmt *stmt = p.statements; stmt != 0; stmt = stmt->next, i++)
  {
    test_assert_m(stmt->data.expr.expr->tag == AstExpr_Identifier, "expected identifier expression");
    test_assert_m(
    str8_equal(stmt->data.expr.expr->data.identifier.token.value, expected_identifiers[i]),
    "expected identifier expression '%.*s', parsed '%.*s'",
    str8_va(expected_identifiers[i]),
    str8_va(stmt->data.expr.token.value));
  }

  test_assert_m(arr_count(expected_identifiers) == i,
                "expected %zu statements, parsed %zu",
                arr_count(expected_identifiers),
                i);

  return 0;
}

internal int
test_parse_expr_number(void)
{
  Str8 input = str8_lit("42069;");

  Str8 expected_numbers[] = {
    str8_cti("42069"),
  };

  Parser p = parse(input);
  parse_print_messages(&p);

  test_assert_m(p.message_list.level <= MessageLevel_Info, "unexpected parse errors");

  usize i = 0;
  for (AstStmt *stmt = p.statements; stmt != 0; stmt = stmt->next, i++)
  {
    test_assert_m(stmt->data.expr.expr->tag == AstExpr_Number, "expected number expression");
    test_assert_m(str8_equal(stmt->data.expr.expr->data.number.token.value, expected_numbers[i]),
                  "expected identifier expression '%.*s', parsed '%.*s'",
                  str8_va(expected_numbers[i]),
                  str8_va(stmt->data.expr.token.value));
    test_assert(stmt->data.expr.expr->data.number.value == 42069);
  }

  test_assert_m(arr_count(expected_numbers) == i,
                "expected %zu statements, parsed %zu",
                arr_count(expected_numbers),
                i);

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

  return result;
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
    lex_print(input);

    print_prompt = input.buf[input.len - 1] == '\n';
  }
}
