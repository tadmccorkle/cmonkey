#include "base/base.h"
#include "lex.h"
#include "parse.h"
#include "eval.h"

#include "base/base.c"
#include "lex.c"
#include "parse.c"
#include "eval.c"

internal int rlpl(void);
internal int rppl(void);
internal int repl(void);

int
main(int argc, char *argv[])
{
  if (argc > 1)
  {
    if (strcmp(argv[1], "lex") == 0)
    {
      return rlpl();
    }

    if (strcmp(argv[1], "parse") == 0)
    {
      return rppl();
    }
  }

  return repl();
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
      Str8 stmt_string = str8_build(&b);
      printf("%.*s\n", str8_va(stmt_string));
    }

    scratch_end(scratch);

    print_prompt = input.buf[input.len - 1] == '\n';
  }
}

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

    if (str8_equal(str8_trim(input), str8_lit("exit")))
    {
      return 0;
    }

    Parser p = parse(arena, input);

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
