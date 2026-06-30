#ifndef CMONKEY_PARSE_H
#define CMONKEY_PARSE_H

#include "base/base.h"
#include "lex.h"

typedef enum
{
  MessageLevel_None,
  MessageLevel_Trace,
  MessageLevel_Info,
  MessageLevel_Warn,
  MessageLevel_Error,
  MessageLevel_Critical,
} MessageLevel;

internal Str8 message_level_name(MessageLevel level);

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
  X(Hash)                 \
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

internal Str8 ast_expr_name(AstExprKind kind);

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

typedef struct AstExprHashPair AstExprHashPair;
struct AstExprHashPair
{
  AstExprHashPair *next;
  AstExpr *key;
  AstExpr *val;
};

typedef struct AstExprHash AstExprHash;
struct AstExprHash
{
  Token token; // The '{' token
  AstExprHashPair *pairs;
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
    AstExprHash hash;
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

internal void parse_init(Parser *p, Arena *arena, Str8 input);
internal Parser parse(Arena *arena, Str8 input);

internal const u8 EXPR_STRING_EMPTY_BUFFER[] = { 0 };

internal void parse_print_messages(Parser *p);

internal void parse_build_stmt_string(StrBuilder8 *b, AstStmt *statements);
internal void parse_build_expr_string(StrBuilder8 *b, AstExpr *expr);

#endif // CMONKEY_PARSE_H
