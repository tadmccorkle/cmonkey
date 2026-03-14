#include <ctype.h>
#include <stdatomic.h>
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

#define align_up(p, a) (((u64)(p) + ((u64)(a) - 1)) & (~((u64)(a) - 1)))

#define arr_count(a) sizeof(a) / sizeof(*a)

#define DEFER_LOOP(begin, end) for (int _i_ = ((begin), 0); !_i_; _i_ += 1, (end))

typedef char *cstr;
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

internal u64
cstr_length(cstr c)
{
  cstr p = c;
  for (; *p != 0; p++);
  return (p - c);
}

typedef struct Str8 Str8;
struct Str8
{
  u8 *buf;
  u64 len;
};

#define str8_lit(S) (Str8){ (u8 *)(S), sizeof(S) - 1 }
#define str8_cti(S) { (u8 *)(S), sizeof(S) - 1 }
#define str8_fmt(S) (u32)(S).len, (S).buf

internal Str8
str8(u8 *value, u64 length)
{
  return (Str8){ value, length };
}

internal Str8
str8_from_cstr(cstr c)
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
  u8 *data;
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
  for (u32 i = 0; i < str.len; i++)
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
  X(TokenType_Illegal)         \
  X(TokenType_EOF)             \
                               \
  /* identifiers & literals */ \
  X(TokenType_Identifier)      \
  X(TokenType_Number)          \
                               \
  /* operators */              \
  X(TokenType_Assign)          \
  X(TokenType_Plus)            \
  X(TokenType_Minus)           \
  X(TokenType_Bang)            \
  X(TokenType_Star)            \
  X(TokenType_Slash)           \
  X(TokenType_Less)            \
  X(TokenType_Greater)         \
  X(TokenType_Equal)           \
  X(TokenType_NotEqual)        \
                               \
  /* delimiters */             \
  X(TokenType_Comma)           \
  X(TokenType_Semicolon)       \
  X(TokenType_LParen)          \
  X(TokenType_RParen)          \
  X(TokenType_LBrace)          \
  X(TokenType_RBrace)          \
  X(TokenType_LBracket)        \
  X(TokenType_RBracket)        \
                               \
  /* keywords */               \
  X(TokenType_Function)        \
  X(TokenType_Let)             \
  X(TokenType_True)            \
  X(TokenType_False)           \
  X(TokenType_If)              \
  X(TokenType_Else)            \
  X(TokenType_Return)

typedef enum TokenType
{
#define TOKEN_KIND(name) name,
  TOKEN_KINDS(TOKEN_KIND)
#undef TOKEN_KIND
  TokenType_COUNT
} TokenType;

internal Str8
token_name(TokenType tokenType)
{
  switch (tokenType)
  {
#define TOKEN_KIND(name) \
  case name: return str8_lit(#name);
    TOKEN_KINDS(TOKEN_KIND)
#undef TOKEN_KIND
    default: return str8_lit("Unknown Token Type");
  }
}

typedef struct Token Token;
struct Token
{
  Str8 value;
  TokenType type;
};

typedef struct LexResult LexResult;
struct LexResult
{
  Token *tokens;
  u32 count;
  b32 is_valid;
};

Token keywords[] = {
  {     str8_cti("fn"), TokenType_Function },
  {    str8_cti("let"),      TokenType_Let },
  {   str8_cti("true"),     TokenType_True },
  {  str8_cti("false"),    TokenType_False },
  {     str8_cti("if"),       TokenType_If },
  {   str8_cti("else"),     TokenType_Else },
  { str8_cti("return"),   TokenType_Return },
};

typedef struct Lexer Lexer;
struct Lexer
{
  Str8 input;
  u32 pos;
};

internal void
lex_free(LexResult l)
{
  free(l.tokens);
}

internal void
lex_print(LexResult lex_result)
{
  for (u32 i = 0; i < lex_result.count; i++)
  {
    Token token = lex_result.tokens[i];
    Str8 name   = token_name(token.type);
    printf("%2d, %-30.*s %.*s\n", token.type, str8_fmt(name), str8_fmt(token.value));
  }
}

internal u8
lex_peek_char(Lexer *lexer)
{
  u32 next_pos = lexer->pos + 1;
  if (next_pos >= lexer->input.len)
  {
    return 0;
  }
  return lexer->input.buf[next_pos];
}

internal void
lex_push_token(Lexer *lexer, LexResult *lex_result, u32 length, TokenType type)
{
  lex_result->tokens[lex_result->count] = (Token){
    .value = str8(&lexer->input.buf[lexer->pos], length),
    .type  = type,
  };
  lex_result->count += 1;
  lexer->pos += length;
}

internal void
lex_consume_whitespace(Lexer *l)
{
  while (l->pos < l->input.len && is_whitespace(l->input.buf[l->pos]))
  {
    l->pos += 1;
  }
}

// TODO(tad): lexing can still operate on a string input (e.g., a file loaded from disk or interpreter input),
// but it would be easier to use if its API was stream-like. Add functions for next_token, peek_token, etc.

/*
internal void
lex_init(Lexer *l, Str8 input)
{
  l->input = input;
  l->pos   = 0;
}

internal void
lex_advance_token(Lexer *l, Token *token)
{
  LexResult result = { 0 };

    lex_consume_whitespace(l);

    if (l->pos >= l->input.len)
    {
      lex_push_token(l, &result, 0, TokenType_EOF);
    }

    if (result.count == capacity)
    {
      capacity *= 2;
      result.tokens = (Token *)realloc(result.tokens, sizeof(*result.tokens) * capacity);
    }

    u8 ch = l.input.buf[l.pos];
    switch (ch)
    {
      case '=':
      {
        if (lex_peek_char(&l) == '=')
        {
          lex_push_token(&l, &result, 2, TokenType_Equal);
        }
        else
        {
          lex_push_token(&l, &result, 1, TokenType_Assign);
        }
        break;
      }
      case '!':
      {
        if (lex_peek_char(&l) == '=')
        {
          lex_push_token(&l, &result, 2, TokenType_NotEqual);
        }
        else
        {
          lex_push_token(&l, &result, 1, TokenType_Bang);
        }
        break;
      }
      case '+': lex_push_token(&l, &result, 1, TokenType_Plus); break;
      case '-': lex_push_token(&l, &result, 1, TokenType_Minus); break;
      case '*': lex_push_token(&l, &result, 1, TokenType_Star); break;
      case '/': lex_push_token(&l, &result, 1, TokenType_Slash); break;
      case '<': lex_push_token(&l, &result, 1, TokenType_Less); break;
      case '>': lex_push_token(&l, &result, 1, TokenType_Greater); break;
      case ';': lex_push_token(&l, &result, 1, TokenType_Semicolon); break;
      case '(': lex_push_token(&l, &result, 1, TokenType_LParen); break;
      case ')': lex_push_token(&l, &result, 1, TokenType_RParen); break;
      case '{': lex_push_token(&l, &result, 1, TokenType_LBrace); break;
      case '}': lex_push_token(&l, &result, 1, TokenType_RBrace); break;
      case ',': lex_push_token(&l, &result, 1, TokenType_Comma); break;
      default:
      {
        u32 length = 0;
        u32 pos    = l.pos;

        if (is_digit(ch))
        {
          do length += 1;
          while (pos < l.input.len && is_digit(l.input.buf[++pos]));

          lex_push_token(&l, &result, length, TokenType_Number);
        }
        else if (ch == '_' || is_letter(ch))
        {
          do
          {
            length += 1;
            ch = l.input.buf[++pos];
          } while (pos < l.input.len && (ch == '_' || is_letter(ch) || is_digit(ch)));

          TokenType type = TokenType_Identifier;
          for (u32 i = 0; i < arr_count(keywords); i++)
          {
            Str8 keyword = keywords[i].value;

            if (keyword.len > length) continue;

            if (!memcmp(keyword.buf, &l.input.buf[l.pos], keyword.len))
            {
              type = keywords[i].type;
            }
          }

          lex_push_token(&l, &result, length, type);
        }
        else
        {
          lex_push_token(&l, &result, 1, TokenType_Illegal);
        }
        break;
      }
    }
}
*/

internal LexResult
lex(Str8 input)
{
  Lexer l          = { .input = input };
  LexResult result = { 0 };

  u64 capacity  = 1024;
  result.tokens = (Token *)malloc(sizeof(*result.tokens) * capacity);

  for (;;)
  {
    lex_consume_whitespace(&l);

    if (l.pos >= input.len)
    {
      if (result.count == capacity)
      {
        result.tokens = (Token *)realloc(result.tokens, sizeof(*result.tokens) + 1);
      }

      lex_push_token(&l, &result, 0, TokenType_EOF);
      break;
    }

    if (result.count == capacity)
    {
      capacity *= 2;
      result.tokens = (Token *)realloc(result.tokens, sizeof(*result.tokens) * capacity);
    }

    u8 ch = l.input.buf[l.pos];
    switch (ch)
    {
      case '=':
      {
        if (lex_peek_char(&l) == '=')
        {
          lex_push_token(&l, &result, 2, TokenType_Equal);
        }
        else
        {
          lex_push_token(&l, &result, 1, TokenType_Assign);
        }
        break;
      }
      case '!':
      {
        if (lex_peek_char(&l) == '=')
        {
          lex_push_token(&l, &result, 2, TokenType_NotEqual);
        }
        else
        {
          lex_push_token(&l, &result, 1, TokenType_Bang);
        }
        break;
      }
      case '+': lex_push_token(&l, &result, 1, TokenType_Plus); break;
      case '-': lex_push_token(&l, &result, 1, TokenType_Minus); break;
      case '*': lex_push_token(&l, &result, 1, TokenType_Star); break;
      case '/': lex_push_token(&l, &result, 1, TokenType_Slash); break;
      case '<': lex_push_token(&l, &result, 1, TokenType_Less); break;
      case '>': lex_push_token(&l, &result, 1, TokenType_Greater); break;
      case ';': lex_push_token(&l, &result, 1, TokenType_Semicolon); break;
      case '(': lex_push_token(&l, &result, 1, TokenType_LParen); break;
      case ')': lex_push_token(&l, &result, 1, TokenType_RParen); break;
      case '{': lex_push_token(&l, &result, 1, TokenType_LBrace); break;
      case '}': lex_push_token(&l, &result, 1, TokenType_RBrace); break;
      case ',': lex_push_token(&l, &result, 1, TokenType_Comma); break;
      default:
      {
        u32 length = 0;
        u32 pos    = l.pos;

        if (is_digit(ch))
        {
          do length += 1;
          while (pos < l.input.len && is_digit(l.input.buf[++pos]));

          lex_push_token(&l, &result, length, TokenType_Number);
        }
        else if (ch == '_' || is_letter(ch))
        {
          do
          {
            length += 1;
            ch = l.input.buf[++pos];
          } while (pos < l.input.len && (ch == '_' || is_letter(ch) || is_digit(ch)));

          TokenType type = TokenType_Identifier;
          for (u32 i = 0; i < arr_count(keywords); i++)
          {
            Str8 keyword = keywords[i].value;

            if (keyword.len > length) continue;

            if (!memcmp(keyword.buf, &l.input.buf[l.pos], keyword.len))
            {
              type = keywords[i].type;
            }
          }

          lex_push_token(&l, &result, length, type);
        }
        else
        {
          lex_push_token(&l, &result, 1, TokenType_Illegal);
        }
        break;
      }
    }
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
// parse

typedef enum
{
  MessageKind_None,
  MessageKind_Trace,
  MessageKind_Info,
  MessageKind_Warn,
  MessageKind_Error,
  MessageKind_Critical,
  MessageKind_COUNT
} MessageLevel;

typedef struct Message Message;
struct Message
{
  Message *next;
  MessageLevel level;
  Str8 value;
};

typedef struct MessageList MessageList;
struct MessageList
{
  Message *messages;
  MessageLevel level;
};

typedef enum
{
  AstStmt_Let,
  // ...
  AstStmt_COUNT,
} AstStmtType;

typedef enum
{
  AstExpr_Identifier,
  // ...
  AstExpr_COUNT,
} AstExprType;

typedef struct AstExprIdentifier AstExprIdentifier;
struct AstExprIdentifier
{
  Token token;
  // TODO(tad): include Str8 Value property?
};

typedef struct AstExpr AstExpr;
struct AstExpr
{
  AstExprType tag;
  union
  {
    AstExprIdentifier identifier;
    // ...
  } data;
};

typedef struct AstStmt AstStmt;
struct AstStmt
{
  AstStmtType tag;
  AstStmt *next;
  union
  {
    struct Let
    {
      Token token;
      AstExprIdentifier *identifier;
      AstExpr *expression;
    } let;
    // ...
  } data;
};

typedef struct Parser Parser;
struct Parser
{
  LexResult l;
  u32 pos;
};

typedef struct ParseResult ParseResult;
struct ParseResult
{
  AstStmt *statements;
  MessageList messages;

  Arena *arena;
};

internal void
parse_free(ParseResult p)
{
  arena_free(p.arena);
}

internal ParseResult
parse(LexResult lex_result)
{
  Parser p           = { .l = lex_result };
  ParseResult result = { .arena = arena_alloc(KiB(4)) };

  AstStmt sentinel = { 0 };
  AstStmt *tail    = &sentinel;

  while (p.pos < p.l.count)
  {
    AstStmt *stmt;

    switch (p.l.tokens[p.pos].type)
    {
      case TokenType_Let:
      {
        stmt      = arena_push_t(result.arena, AstStmt);
        stmt->tag = AstStmt_Let;

        AstExprIdentifier *identifier = arena_push_t(result.arena, AstExprIdentifier);
        identifier->token             = p.l.tokens[p.pos + 1];

        stmt->data.let = (struct Let){
          .token      = p.l.tokens[p.pos],
          .identifier = identifier,
          //.expression = // TODO(tad)
        };

        break;
      }

      case TokenType_Illegal:
      case TokenType_EOF:
      case TokenType_Identifier:
      case TokenType_Number:
      case TokenType_Assign:
      case TokenType_Plus:
      case TokenType_Minus:
      case TokenType_Bang:
      case TokenType_Star:
      case TokenType_Slash:
      case TokenType_Less:
      case TokenType_Greater:
      case TokenType_Equal:
      case TokenType_NotEqual:
      case TokenType_Comma:
      case TokenType_Semicolon:
      case TokenType_LParen:
      case TokenType_RParen:
      case TokenType_LBrace:
      case TokenType_RBrace:
      case TokenType_LBracket:
      case TokenType_RBracket:
      case TokenType_Function:
      case TokenType_True:
      case TokenType_False:
      case TokenType_If:
      case TokenType_Else:
      case TokenType_Return:
        // TODO(tad): not implemented
        printf("Unsupported token encountered: %.*s", str8_fmt(token_name(p.l.tokens[p.pos - 1].type)));
        break;

      case TokenType_COUNT:
      default:
        // TODO(tad): invalid case
        printf("Invalid token encountered: %.*s", str8_fmt(token_name(p.l.tokens[p.pos - 1].type)));
        break;
    }

    tail->next = stmt;
    tail       = stmt;
  }

  result.statements = sentinel.next;

  return result;
}

////////////////////////////////////////////////////////////////////////////////
// main

internal Str8 test_get_input(void);
internal int test(void);
internal int repl(void);

int
main(int argc, cstr *argv)
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
    input.buf = (u8 *)malloc(sizeof(*input.buf) * input.len);
    fread(input.buf, input.len, 1, f);
    fclose(f);
  }
  else
  {
    input = test_get_input();
  }

  LexResult lex_result = lex(input);

  printf("%.*s", str8_fmt(input));
  printf("\n---------------\n\n");
  lex_print(lex_result);

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
// test

#define test_assert_m(test, fmt, ...)  \
  do                                   \
  {                                    \
    if (!(test))                       \
    {                                  \
      printf(fmt "\n", ##__VA_ARGS__); \
      return 1;                        \
    }                                  \
  } while (0)
#define test_assert(test) test_assert_m(test, "failure: test error");

Str8 t_lex_input = str8_lit("let five = 5;\n"
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
                            "\n");

#define test_check_lex(type, literal) { str8_cti(literal), type }

internal int
test_lex(void)
{
  Token test_results[] = {
    test_check_lex(TokenType_Let, "let"),        test_check_lex(TokenType_Identifier, "five"),
    test_check_lex(TokenType_Assign, "="),       test_check_lex(TokenType_Number, "5"),
    test_check_lex(TokenType_Semicolon, ";"),    test_check_lex(TokenType_Let, "let"),
    test_check_lex(TokenType_Identifier, "ten"), test_check_lex(TokenType_Assign, "="),
    test_check_lex(TokenType_Number, "10"),      test_check_lex(TokenType_Semicolon, ";"),

    test_check_lex(TokenType_Let, "let"),        test_check_lex(TokenType_Identifier, "add"),
    test_check_lex(TokenType_Assign, "="),       test_check_lex(TokenType_Function, "fn"),
    test_check_lex(TokenType_LParen, "("),       test_check_lex(TokenType_Identifier, "x"),
    test_check_lex(TokenType_Comma, ","),        test_check_lex(TokenType_Identifier, "y"),
    test_check_lex(TokenType_RParen, ")"),       test_check_lex(TokenType_LBrace, "{"),
    test_check_lex(TokenType_Identifier, "x"),   test_check_lex(TokenType_Plus, "+"),
    test_check_lex(TokenType_Identifier, "y"),   test_check_lex(TokenType_Semicolon, ";"),
    test_check_lex(TokenType_RBrace, "}"),       test_check_lex(TokenType_Semicolon, ";"),

    test_check_lex(TokenType_Let, "let"),        test_check_lex(TokenType_Identifier, "result"),
    test_check_lex(TokenType_Assign, "="),       test_check_lex(TokenType_Identifier, "add"),
    test_check_lex(TokenType_LParen, "("),       test_check_lex(TokenType_Identifier, "five"),
    test_check_lex(TokenType_Comma, ","),        test_check_lex(TokenType_Identifier, "ten"),
    test_check_lex(TokenType_RParen, ")"),       test_check_lex(TokenType_Semicolon, ";"),

    test_check_lex(TokenType_Bang, "!"),         test_check_lex(TokenType_Minus, "-"),
    test_check_lex(TokenType_Slash, "/"),        test_check_lex(TokenType_Star, "*"),
    test_check_lex(TokenType_Number, "5"),       test_check_lex(TokenType_Semicolon, ";"),
    test_check_lex(TokenType_Number, "5"),       test_check_lex(TokenType_Less, "<"),
    test_check_lex(TokenType_Number, "10"),      test_check_lex(TokenType_Greater, ">"),
    test_check_lex(TokenType_Number, "5"),       test_check_lex(TokenType_If, "if"),
    test_check_lex(TokenType_LParen, "("),       test_check_lex(TokenType_Number, "5"),
    test_check_lex(TokenType_Less, "<"),         test_check_lex(TokenType_Number, "10"),
    test_check_lex(TokenType_RParen, ")"),       test_check_lex(TokenType_LBrace, "{"),
    test_check_lex(TokenType_Return, "return"),  test_check_lex(TokenType_True, "true"),
    test_check_lex(TokenType_Semicolon, ";"),    test_check_lex(TokenType_RBrace, "}"),
    test_check_lex(TokenType_Else, "else"),      test_check_lex(TokenType_LBrace, "{"),
    test_check_lex(TokenType_Return, "return"),  test_check_lex(TokenType_False, "false"),
    test_check_lex(TokenType_Semicolon, ";"),    test_check_lex(TokenType_RBrace, "}"),

    test_check_lex(TokenType_Number, "10"),      test_check_lex(TokenType_Equal, "=="),
    test_check_lex(TokenType_Number, "10"),      test_check_lex(TokenType_Number, "10"),
    test_check_lex(TokenType_NotEqual, "!="),    test_check_lex(TokenType_Number, "9"),
  };

  LexResult lex_result = lex(t_lex_input);

  //test_assert_m("failure: invalid lex", lexResult.is_valid);

  test_assert_m(arr_count(test_results) == lex_result.count,
                "failure: wrong lex result count - expected=%lu, actual=%d",
                arr_count(test_results),
                lex_result.count);

  for (u32 i = 0; i < lex_result.count; i++)
  {
    Token actual   = lex_result.tokens[i];
    Token expected = test_results[i];

    test_assert_m(actual.type == expected.type,
                  "failure: test[%d] - wrong token type - expected=%.*s, actual=%.*s",
                  i,
                  str8_fmt(token_name(expected.type)),
                  str8_fmt(token_name(actual.type)));

    test_assert_m(str8_equal(actual.value, expected.value),
                  "failure: test[%d] - wrong token value - expected='%.*s', actual='%.*s'",
                  i,
                  str8_fmt(expected.value),
                  str8_fmt(actual.value));
  }

  lex_free(lex_result);

  return 0;
}

internal int
test_parse_let(void)
{
  Str8 input = str8_lit("let x = 5; let y = 10;");

  Str8 expectedIdentifiers[] = {
    str8_cti("x"),
    str8_cti("y"),
  };

  LexResult lex_result     = lex(input);
  ParseResult parse_result = parse(lex_result);

  usize i = 0;
  for (AstStmt *stmt = parse_result.statements; stmt != 0; stmt = stmt->next)
  {
    test_assert(stmt->tag == AstStmt_Let);
    test_assert(str8_equal(stmt->data.let.identifier->token.value, expectedIdentifiers[i]));

    i += 1;
  }

  test_assert(arr_count(expectedIdentifiers) == i);

  lex_free(lex_result);
  parse_free(parse_result);

  return 0;
}

internal Str8
test_get_input(void)
{
  return t_lex_input;
}

internal int
test(void)
{
  int result = 0;

  result += test_lex();
  result += test_parse_let();

  return result;
}

////////////////////////////////////////////////////////////////////////////////
// repl

internal int
repl(void)
{
  const cstr PROMPT = ">> ";
  char buffer[KiB(4)];
  b32 print_prompt = 1;

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

    Str8 input           = str8_from_cstr(buffer);
    LexResult lex_result = lex(input);

    lex_print(lex_result);

    print_prompt = input.buf[input.len - 1] == '\n';

    if (lex_result.count == 0 && !print_prompt && !all_whitespace(input)) printf("\n");

    lex_free(lex_result);
  }
}
