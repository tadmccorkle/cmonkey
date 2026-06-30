#ifndef CMONKEY_LEX_H
#define CMONKEY_LEX_H

#include "base/base.h"

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
  X(Colon)                     \
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

typedef struct Token Token;
struct Token
{
  TokenKind kind;
  Str8 value;
};

internal Str8 token_name(TokenKind kind);

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

internal void lex_init(Lexer *l, Str8 input);
internal void lex_consume_whitespace(Lexer *l);
internal u8 lex_peek_char(Lexer const *l);
internal void lex_init_token(Lexer *l, Token *token, usize length, TokenKind kind);
internal void lex_advance_token(Lexer *l, Token *token);

#endif // CMONKEY_LEX_H
