#include "test.h"

#include "base/base.h"
#include "lex.h"
#include "parse.h"
#include "eval.h"

#include "base/base.c"
#include "lex.c"
#include "parse.c"
#include "eval.c"

internal int test(void);

int
main(void)
{
  return test();
}

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
                        "!-/*5:;\n"
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
    test_result(TokenKind_Colon, ":"),
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
test_string_expr(AstExpr *expr, Str8 expected)
{
  test_assert_m(expr != 0, "expected string expression is null");
  test_assert_m(expr->tag == AstExpr_String, "expected string expression");

  Str8 actual = expr->data.string.value;
  test_assert_m(str8_equal(actual, expected),
                "expected string expression '%.*s', parsed '%.*s'",
                str8_va(expected),
                str8_va(actual));

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
    case AstExpr_String: test_helper(test_string_expr(expr, *(Str8 *)expected)); break;

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
  // empty: []
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
test_parse_expr_hash(void)
{
  // empty: {}
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, str8_lit("{}"));
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected hash expression statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");

    AstExpr *expr = stmt->data.expr.expr;
    test_assert_m(expr != 0, "expected hash expression is null");
    test_assert_m(expr->tag == AstExpr_Hash, "expected hash expression");
    test_assert_m(expr->data.hash.pairs == 0, "expected empty hash, parsed elements");

    scratch_end(scratch);
  }

  // single element: {"key": 1}
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, str8_lit("{\"key\": 1}"));
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected hash expression statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");

    AstExpr *expr = stmt->data.expr.expr;
    test_assert_m(expr != 0, "expected hash expression is null");
    test_assert_m(expr->tag == AstExpr_Hash, "expected hash expression");

    AstExprHashPair *element = expr->data.hash.pairs;
    test_assert_m(element != 0, "expected one hash element, parsed zero");
    test_helper(test_lit_expr(element->key, expected_lit(AstExpr_String, Str8, str8_lit("key"))));
    test_helper(test_lit_expr(element->val, expected_lit(AstExpr_Number, s64, 1)));
    test_assert_m(element->next == 0, "expected one hash element, parsed more");

    scratch_end(scratch);
  }

  // multiple elements: {fn(){"one"}(): 1, true: 1 + 1, 3: 15 / 5}
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, str8_lit("{fn(){\"one\"}: 1, true: 1 + 1, 3: 15 / 5}"));
    test_helper(test_parse_check_messages(&p));

    AstStmt *stmt = p.statements;
    test_assert_m(stmt != 0, "expected hash expression statement is null");
    test_assert_m(stmt->next == 0, "expected one statement, parsed more");
    test_assert_m(stmt->tag == AstStmt_Expr, "expected expression statement");

    AstExpr *expr = stmt->data.expr.expr;
    test_assert_m(expr != 0, "expected hash expression is null");
    test_assert_m(expr->tag == AstExpr_Hash, "expected hash expression");

    AstExprHashPair *element = expr->data.hash.pairs;
    test_assert_m(element != 0, "expected three hash elements, parsed zero");
    test_assert_m(element->key->tag == AstExpr_Function, "expected function expression");

    element = element->next;
    test_assert_m(element != 0, "expected three hash elements, parsed fewer");
    test_helper(test_lit_expr(element->key, expected_lit(AstExpr_Boolean, bool, true)));
    test_helper(test_infix_lit_expr(element->val,
                                    TokenKind_Plus,
                                    expected_lit(AstExpr_Number, s64, 1),
                                    expected_lit(AstExpr_Number, s64, 1)));

    element = element->next;
    test_assert_m(element != 0, "expected three hash elements, parsed fewer");
    test_helper(test_lit_expr(element->key, expected_lit(AstExpr_Number, s64, 3)));
    test_helper(test_infix_lit_expr(element->val,
                                    TokenKind_Slash,
                                    expected_lit(AstExpr_Number, s64, 15),
                                    expected_lit(AstExpr_Number, s64, 5)));

    test_assert_m(element->next == 0, "expected three hash elements, parsed more");

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
test_object_hash(void)
{
  TmpArena scratch = scratch_begin(0);

  ObjectHash hash = { 0 };
  object_hash_init(&hash, scratch.arena, 0);

  // mixed key types
  Object const *string_key = object_from_string(scratch.arena, str8_lit("key"));
  Object const *number_key = object_from_number(scratch.arena, 42);
  Object const *bool_key   = object_from_bool(true);

  object_hash_set(scratch.arena, &hash, string_key, object_from_number(scratch.arena, 1));
  object_hash_set(scratch.arena, &hash, number_key, object_from_number(scratch.arena, 2));
  object_hash_set(scratch.arena, &hash, bool_key, object_from_number(scratch.arena, 3));

  test_assert_m(hash.cnt == 3, "expected count of 3, got %u", hash.cnt);

  Object const *string_val = object_hash_get(&hash, string_key);
  test_assert_m(string_val->tag == ObjectKind_Number && string_val->data.number.value == 1,
                "expected string key to map to value 1");

  Object const *number_val = object_hash_get(&hash, number_key);
  test_assert_m(number_val->tag == ObjectKind_Number && number_val->data.number.value == 2,
                "expected number key to map to value 2");

  Object const *bool_val = object_hash_get(&hash, bool_key);
  test_assert_m(bool_val->tag == ObjectKind_Number && bool_val->data.number.value == 3,
                "expected boolean key to map to value 3");

  // distinct key objects with equal values resolve to the same slot
  Object const *string_key_dup = object_from_string(scratch.arena, str8_lit("key"));
  Object const *number_key_dup = object_from_number(scratch.arena, 42);
  test_assert_m(object_hash_get(&hash, string_key_dup) == string_val,
                "expected equal string key to resolve to same value");
  test_assert_m(object_hash_get(&hash, number_key_dup) == number_val,
                "expected equal number key to resolve to same value");

  // overwrite keeps count stable and updates the value
  object_hash_set(scratch.arena, &hash, number_key_dup, object_from_number(scratch.arena, 99));
  test_assert_m(hash.cnt == 3, "expected count to remain 3 after overwrite, got %u", hash.cnt);
  Object const *overwritten = object_hash_get(&hash, number_key);
  test_assert_m(overwritten->tag == ObjectKind_Number && overwritten->data.number.value == 99,
                "expected overwritten value 99, got %lld",
                overwritten->data.number.value);

  // absent key returns OBJECT_NULL
  Object const *absent = object_hash_get(&hash, object_from_string(scratch.arena, str8_lit("missing")));
  test_assert_m(absent == OBJECT_NULL, "expected OBJECT_NULL for absent key");

  // force growth and verify all entries survive the rehash
  u32 initial_cap = hash.cap;

  enum
  {
    insert_count = 64
  };
  for (s64 i = 0; i < insert_count; i++)
  {
    object_hash_set(scratch.arena,
                    &hash,
                    object_from_number(scratch.arena, 1000 + i),
                    object_from_number(scratch.arena, i));
  }

  test_assert_m(hash.cap > initial_cap,
                "expected capacity to grow beyond %u after %d inserts, got %u",
                initial_cap,
                insert_count,
                hash.cap);

  for (s64 i = 0; i < insert_count; i++)
  {
    Object const *key = object_from_number(scratch.arena, 1000 + i);
    Object const *got = object_hash_get(&hash, key);
    test_assert_m(got->tag == ObjectKind_Number && got->data.number.value == i,
                  "expected value %lld for key %lld after grow, got %lld",
                  i,
                  1000 + i,
                  got->data.number.value);
  }

  // original entries still resolve after growth
  test_assert_m(object_hash_get(&hash, string_key)->data.number.value == 1,
                "expected string key to survive grow");
  test_assert_m(object_hash_get(&hash, bool_key)->data.number.value == 3,
                "expected boolean key to survive grow");

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
test_eval_hash_expr(void)
{
  // empty: {}
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, str8_lit("{}"));
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_Hash, "expected hash object");
    test_assert_m(result->data.hash.cnt == 0,
                  "expected empty hash, evaluated %u pairs",
                  result->data.hash.cnt);

    scratch_end(scratch);
  }

  // string keys: {"one": 1, "two": 1 + 1, "three": 6 / 2}
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, str8_lit("{\"one\": 1, \"two\": 1 + 1, \"three\": 6 / 2}"));
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_Hash, "expected hash object");
    test_assert_m(result->data.hash.cnt == 3,
                  "expected three hash pairs, evaluated %u",
                  result->data.hash.cnt);

    struct
    {
      Str8 key;
      s64 expected;
    } expected[] = {
      { str8_lit("one"), 1 },
      { str8_lit("two"), 2 },
      { str8_lit("three"), 3 },
    };

    for (usize i = 0; i < arr_count(expected); i++)
    {
      Object const *key = object_from_string(scratch.arena, expected[i].key);
      Object const *val = object_hash_get(&result->data.hash, key);
      test_assert_m(val->tag == ObjectKind_Number, "expected number hash value");
      test_assert_m(val->data.number.value == expected[i].expected,
                    "expected hash value '%lld' for key '%.*s', evaluated '%lld'",
                    expected[i].expected,
                    str8_va(expected[i].key),
                    val->data.number.value);
    }

    scratch_end(scratch);
  }

  // expression keys of every key type: {"th" + "ree": 3, 1 < 2: 5, 2 * 2: 16, fn() { 10 }(): 100}
  // keys evaluate to: string "three", boolean true, number 4, number 10
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena,
                     str8_lit("{\"th\" + \"ree\": 3, 1 < 2: 5, 2 * 2: 16, fn() { 10 }(): 100}"));
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_Hash, "expected hash object");
    test_assert_m(result->data.hash.cnt == 4,
                  "expected four hash pairs, evaluated %u",
                  result->data.hash.cnt);

    Object const *string_key = object_from_string(scratch.arena, str8_lit("three"));
    Object const *string_val = object_hash_get(&result->data.hash, string_key);
    test_assert_m(string_val->tag == ObjectKind_Number, "expected number hash value for string key");
    test_assert_m(string_val->data.number.value == 3,
                  "expected hash value '3' for string key, evaluated '%lld'",
                  string_val->data.number.value);

    Object const *bool_key = object_from_bool(true);
    Object const *bool_val = object_hash_get(&result->data.hash, bool_key);
    test_assert_m(bool_val->tag == ObjectKind_Number, "expected number hash value for boolean key");
    test_assert_m(bool_val->data.number.value == 5,
                  "expected hash value '5' for boolean key, evaluated '%lld'",
                  bool_val->data.number.value);

    Object const *number_key = object_from_number(scratch.arena, 4);
    Object const *number_val = object_hash_get(&result->data.hash, number_key);
    test_assert_m(number_val->tag == ObjectKind_Number, "expected number hash value for number key");
    test_assert_m(number_val->data.number.value == 16,
                  "expected hash value '16' for number key, evaluated '%lld'",
                  number_val->data.number.value);

    Object const *call_key = object_from_number(scratch.arena, 10);
    Object const *call_val = object_hash_get(&result->data.hash, call_key);
    test_assert_m(call_val->tag == ObjectKind_Number, "expected number hash value for call key");
    test_assert_m(call_val->data.number.value == 100,
                  "expected hash value '100' for call key, evaluated '%lld'",
                  call_val->data.number.value);

    scratch_end(scratch);
  }

  // duplicate keys overwrite: {1: 1, 1: 2}
  {
    TmpArena scratch = scratch_begin(0);

    Parser p = parse(scratch.arena, str8_lit("{1: 1, 1: 2}"));
    test_helper(test_parse_check_messages(&p));

    Env env = { 0 };
    env_init(&env, scratch.arena, 0);

    Object const *result = eval_program(scratch.arena, p.statements, &env);
    test_assert_m(result->tag == ObjectKind_Hash, "expected hash object");
    test_assert_m(result->data.hash.cnt == 1,
                  "expected one hash pair after overwrite, evaluated %u",
                  result->data.hash.cnt);

    Object const *key = object_from_number(scratch.arena, 1);
    Object const *val = object_hash_get(&result->data.hash, key);
    test_assert_m(val->tag == ObjectKind_Number, "expected number hash value");
    test_assert_m(val->data.number.value == 2,
                  "expected overwritten hash value '2', evaluated '%lld'",
                  val->data.number.value);

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_eval_index_expr_array(void)
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
test_eval_index_expr_string(void)
{
  struct
  {
    Str8 input;
    Str8 expected;
    b8 is_null;
  } tests[] = {
    { str8_lit("\"test\"[0]"), str8_lit("t"), false },
    { str8_lit("\"test\"[1]"), str8_lit("e"), false },
    { str8_lit("\"test\"[2]"), str8_lit("s"), false },
    { str8_lit("\"test\"[4]"), { 0 }, true },
    { str8_lit("\"test\"[-1]"), { 0 }, true },
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
      test_assert_m(result->tag == ObjectKind_String, "expected string object");
      test_assert_m(str8_equal(result->data.string.value, tests[i].expected),
                    "expected number value '%.*s', evaluated '%.*s'",
                    str8_va(tests[i].expected),
                    str8_va(result->data.string.value));
    }

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_eval_index_expr_hash(void)
{
  struct
  {
    Str8 input;
    s64 expected;
    b8 is_null;
  } tests[] = {
    { str8_lit("{\"foo\": 5}[\"foo\"]"), 5, false },
    { str8_lit("{\"foo\": 5}[\"bar\"]"), 0, true },
    { str8_lit("{}[\"foo\"]"), 0, true },
    { str8_lit("{5: 5}[5]"), 5, false },
    { str8_lit("{true: 5}[true]"), 5, false },
    { str8_lit("{false: 5}[false]"), 5, false },
    { str8_lit("{\"foo\": 5}[\"f\" + \"oo\"]"), 5, false },
    { str8_lit("{1: 1, 2: 2}[1 + 1]"), 2, false },
    { str8_lit("let key = \"foo\"; {\"foo\": 5}[key]"), 5, false },
    { str8_lit("{\"foo\": 5}[fn() { \"foo\" }()]"), 5, false },
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
      str8_lit("len([])"),
      .expected = 0,
    },
    {
      str8_lit("len([2])"),
      .expected = 1,
    },
    {
      str8_lit("len([2,5,8])"),
      .expected = 3,
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
test_eval_builtin_rest(void)
{
  struct
  {
    Str8 input;
    s64 expected[8];
    usize expected_len;
    b8 is_null;
    Str8 err;
  } tests[] = {
    {
      str8_lit("rest([1, 2, 3])"),
      .expected     = { 2, 3 },
      .expected_len = 2,
    },
    {
      str8_lit("rest([2, 4, 6, 8])"),
      .expected     = { 4, 6, 8 },
      .expected_len = 3,
    },
    {
      str8_lit("rest([1])"),
      .expected_len = 0,
    },
    {
      str8_lit("rest([])"),
      .is_null = true,
    },
    {
      str8_lit("rest(1)"),
      .err = str8_lit("argument to builtin 'rest' not supported: Number"),
    },
    {
      str8_lit("rest()"),
      .err = str8_lit("builtin 'rest' requires 1 argument, received: 0"),
    },
    {
      str8_lit("rest([1], [2])"),
      .err = str8_lit("builtin 'rest' requires 1 argument, received: 2"),
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
    if (tests[i].err.buf != 0)
    {
      test_assert_m(result->tag == ObjectKind_Error, "expected error object");
      test_assert_m(str8_equal(tests[i].err, result->data.err.value),
                    "expected error '%.*s', evaluated '%.*s'",
                    str8_va(tests[i].err),
                    str8_va(result->data.err.value));
    }
    else if (tests[i].is_null)
    {
      test_assert_m(result->tag == ObjectKind_Null,
                    "expected null object, evaluated '%.*s'",
                    str8_va(object_name(result->tag)));
    }
    else
    {
      test_assert_m(result->tag == ObjectKind_Array, "expected array object");
      test_assert_m(result->data.array.len == tests[i].expected_len,
                    "expected %zu array elements, evaluated %zu",
                    tests[i].expected_len,
                    result->data.array.len);

      for (usize j = 0; j < tests[i].expected_len; j++)
      {
        Object const *element = result->data.array.elements[j];
        test_assert_m(element->tag == ObjectKind_Number, "expected number array element");
        test_assert_m(element->data.number.value == tests[i].expected[j],
                      "expected array element '%lld', evaluated '%lld'",
                      tests[i].expected[j],
                      element->data.number.value);
      }
    }

    scratch_end(scratch);
  }

  return 0;
}

internal int
test_eval_builtin_push(void)
{
  struct
  {
    Str8 input;
    s64 expected[8];
    usize expected_len;
    Str8 err;
  } tests[] = {
    {
      str8_lit("push([], 1)"),
      .expected     = { 1 },
      .expected_len = 1,
    },
    {
      str8_lit("push([1], 2)"),
      .expected     = { 1, 2 },
      .expected_len = 2,
    },
    {
      str8_lit("push([1, 2], 3)"),
      .expected     = { 1, 2, 3 },
      .expected_len = 3,
    },
    {
      str8_lit("push(1, 2)"),
      .err = str8_lit("argument to builtin 'push' not supported: Number"),
    },
    {
      str8_lit("push()"),
      .err = str8_lit("builtin 'push' requires 2 argument, received: 0"),
    },
    {
      str8_lit("push([1])"),
      .err = str8_lit("builtin 'push' requires 2 argument, received: 1"),
    },
    {
      str8_lit("push([1], 2, 3)"),
      .err = str8_lit("builtin 'push' requires 2 argument, received: 3"),
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
    if (tests[i].err.buf != 0)
    {
      test_assert_m(result->tag == ObjectKind_Error, "expected error object");
      test_assert_m(str8_equal(tests[i].err, result->data.err.value),
                    "expected error '%.*s', evaluated '%.*s'",
                    str8_va(tests[i].err),
                    str8_va(result->data.err.value));
    }
    else
    {
      test_assert_m(result->tag == ObjectKind_Array, "expected array object");
      test_assert_m(result->data.array.len == tests[i].expected_len,
                    "expected %zu array elements, evaluated %zu",
                    tests[i].expected_len,
                    result->data.array.len);

      for (usize j = 0; j < tests[i].expected_len; j++)
      {
        Object const *element = result->data.array.elements[j];
        test_assert_m(element->tag == ObjectKind_Number, "expected number array element");
        test_assert_m(element->data.number.value == tests[i].expected[j],
                      "expected array element '%lld', evaluated '%lld'",
                      tests[i].expected[j],
                      element->data.number.value);
      }
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
      str8_lit("{fn(){}:1}"),
      str8_lit("invalid hash key type: Function"),
    },
    {
      str8_lit("{{}:1}"),
      str8_lit("invalid hash key type: Hash"),
    },
    {
      str8_lit("{[]:1}"),
      str8_lit("invalid hash key type: Array"),
    },
    {
      str8_lit("\"test\"[\"i\"]"),
      str8_lit("invalid string index type: String"),
    },
    {
      str8_lit("{}[fn(){}]"),
      str8_lit("invalid hash index type: Function"),
    },
    {
      str8_lit("{}[{}]"),
      str8_lit("invalid hash index type: Hash"),
    },
    {
      str8_lit("{}[[]]"),
      str8_lit("invalid hash index type: Array"),
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
  result += test_parse_expr_hash();
  result += test_parse_expr_index();

  result += test_parse_op_precedence();

  result += test_env_set_get();
  result += test_env_get_missing();
  result += test_env_overwrite();
  result += test_env_outer_scope();
  result += test_env_grow();

  result += test_object_hash();

  result += test_eval_number_expr();
  result += test_eval_boolean_expr();
  result += test_eval_bang();
  result += test_eval_if_else_expr();
  result += test_eval_function_expr();
  result += test_eval_string_expr();
  result += test_eval_array_expr();
  result += test_eval_hash_expr();
  result += test_eval_index_expr_array();
  result += test_eval_index_expr_string();
  result += test_eval_index_expr_hash();

  result += test_eval_builtin_len();
  result += test_eval_builtin_rest();
  result += test_eval_builtin_push();

  result += test_eval_return_stmt();
  result += test_eval_let_stmt();

  result += test_eval_error_handling();

  return result;
}
