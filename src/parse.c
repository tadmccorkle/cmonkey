#include <errno.h>
#include <stdlib.h>

#include "parse.h"

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

internal void
parse_init(Parser *p, Arena *arena, Str8 input)
{
  p->arena = arena;

  lex_init(&p->l, input);
  lex_advance_token(&p->l, &p->curr_token);
  lex_advance_token(&p->l, &p->peek_token);
}

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

    case AstExpr_Hash:
      str8_append_lit(b, "{");
      if (expr->data.hash.pairs != 0)
      {
        AstExprHashPair *element = expr->data.hash.pairs;
        for (;;)
        {
          parse_build_expr_string(b, element->key);
          str8_append_lit(b, ":");
          parse_build_expr_string(b, element->val);
          if ((element = element->next) == 0) break;
          str8_append_lit(b, ", ");
        }
      }
      str8_append_lit(b, "}");
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
parse_expr_hash(Parser *p)
{
  AstExpr *hash         = arena_alloc_t(p->arena, AstExpr);
  hash->tag             = AstExpr_Hash;
  hash->data.hash.token = p->curr_token;

  if (p->peek_token.kind != TokenKind_RBrace)
  {
    SLL_BUILDER(AstExprHashPair) elements;
    sll_builder_init(elements);

    for (;;)
    {
      parse_advance_token(p);

      AstExprHashPair *element = arena_alloc_t(p->arena, AstExprHashPair);
      element->key             = parse_expr(p, Precedence_Lowest);

      if (!parse_expect(p, TokenKind_Colon))
      {
        break;
      }
      parse_advance_token(p);

      element->val = parse_expr(p, Precedence_Lowest);

      sll_builder_append(elements, element);

      if (p->peek_token.kind == TokenKind_RBrace)
      {
        parse_advance_token(p);
        break;
      }

      if (!parse_expect(p, TokenKind_Comma))
      {
        break;
      }
    }

    hash->data.hash.pairs = sll_builder_result(elements);
    hash->data.hash.count = elements.count;
  }
  else
  {
    parse_advance_token(p);
  }

  return hash;
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
    case TokenKind_LBrace: return parse_expr_hash(p);
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
