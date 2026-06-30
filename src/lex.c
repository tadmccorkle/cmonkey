#include "lex.h"

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

internal void
lex_init(Lexer *l, Str8 input)
{
  l->input = input;
  l->pos   = 0;
}

internal void
lex_consume_whitespace(Lexer *l)
{
  while (l->pos < l->input.len && u8_is_space(l->input.buf[l->pos]))
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

internal bool
is_escapable_char(u8 ch)
{
  return ch == '\\' || ch == '"' || ch == 't' || ch == 'n';
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
    case ':': lex_init_token(l, token, 1, TokenKind_Colon); break;
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
      if (u8_is_digit(ch))
      {
        usize len = 1;
        while (l->pos + len < l->input.len && u8_is_digit(l->input.buf[l->pos + len]))
        {
          len += 1;
        }

        lex_init_token(l, token, len, TokenKind_Number);
      }
      else if (ch == '_' || u8_is_letter(ch))
      {
        usize len = 1;
        while (l->pos + len < l->input.len)
        {
          ch = l->input.buf[l->pos + len];
          if (ch != '_' && !u8_is_letter(ch) && !u8_is_digit(ch)) break;
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
