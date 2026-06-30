#ifndef CMONKEY_EVAL_H
#define CMONKEY_EVAL_H

#include "base/base.h"
#include "parse.h"

typedef struct Object Object;

// =============================================================
// environment

#define ENV_LOAD_FACTOR 0.75

typedef struct EnvVar EnvVar;
struct EnvVar
{
  Str8 name;
  Object const *value;
};

typedef struct Env Env;
struct Env
{
  Env *outer;
  Arena *arena;

  u32 cnt;
  u32 cap;
  EnvVar *vars;
};

internal void env_init(Env *env, Arena *arena, u32 initial_capacity);
internal Object const *env_get(Env const *env, Str8 name);
internal void env_set(Env *env, Str8 name, Object const *value);

#define env_init_count(env, arena, initial_count) \
  env_init((env), (arena), ceil_pos(u32, (initial_count) / ENV_LOAD_FACTOR))

// =============================================================
// objects

#define OBJECT_KINDS(X) \
  X(Number)             \
  X(Boolean)            \
  X(String)             \
  X(Array)              \
  X(Hash)               \
  X(Return)             \
  X(Function)           \
  X(Builtin)            \
  X(Null)               \
  X(Error)

typedef enum
{
#define OBJECT_KIND(name) ObjectKind_##name,
  OBJECT_KINDS(OBJECT_KIND)
#undef OBJECT_KIND
} ObjectKind;

internal Str8 object_name(ObjectKind kind);

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

typedef struct ObjectString ObjectString;
struct ObjectString
{
  Str8 value;
};

typedef struct ObjectArray ObjectArray;
struct ObjectArray
{
  Object const **elements;
  usize len;
};

typedef struct ObjectHashPair ObjectHashPair;
struct ObjectHashPair
{
  Object const *key;
  Object const *val;
};

typedef struct ObjectHash ObjectHash;
struct ObjectHash
{
  u32 cnt;
  u32 cap;
  ObjectHashPair *pairs;
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

typedef struct ObjectFunction ObjectFunction;
struct ObjectFunction
{
  AstExprFunctionParam *params;
  AstStmtBlock *body;
  Env *env;
};

typedef struct ObjectBuiltin ObjectBuiltin;
struct ObjectBuiltin
{
  Str8 name;
  Object const *(*fn)(Arena *, Object const **, u8); // fn(arena, args, arg_count)
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
    ObjectString string;
    ObjectArray array;
    ObjectHash hash;
    ObjectReturn ret;
    ObjectFunction function;
    ObjectBuiltin builtin;
    ObjectNull null;
    ObjectError err;
  } data;
};

internal Object const *OBJECT_TRUE  = &(Object){ ObjectKind_Boolean, { .boolean.value = true } };
internal Object const *OBJECT_FALSE = &(Object){ ObjectKind_Boolean, { .boolean.value = false } };
internal Object const *OBJECT_NULL  = &(Object){ ObjectKind_Null, { 0 } };

internal Object const *object_from_number(Arena *arena, s64 value);
internal Object const *object_from_bool(bool value);
internal Object const *object_from_string(Arena *arena, Str8 value);
internal Object const *object_from_array(Arena *arena, Object const **elements, usize len);
internal Object const *object_from_function(Arena *arena, AstExprFunction fn, Env *env);
internal Object const *object_from_error(Arena *arena, Str8 error);

internal bool object_is_truthy(Object const *object);
internal bool object_is_error(Object const *object);

// =============================================================
// hash objects

#define OBJECT_HASH_LOAD_FACTOR 0.75

internal void object_hash_init(ObjectHash *hash, Arena *arena, u32 initial_capacity);
internal Object const *object_hash_get(ObjectHash const *hash, Object const *key);
internal void object_hash_set(Arena *arena, ObjectHash *hash, Object const *key, Object const *val);

#define object_hash_init_count(hash, arena, initial_count) \
  object_hash_init((hash), (arena), ceil_pos(u32, (initial_count) / OBJECT_HASH_LOAD_FACTOR))

// =============================================================
// evaluation

internal Str8 eval_inspect(Arena *arena, Object const *o);

internal Object const *eval_program(Arena *arena, AstStmt *statements, Env *env);
internal Object const *eval_block(Arena *arena, AstStmtBlock *block, Env *env);
internal Object const *eval_expr(Arena *arena, AstExpr *expr, Env *env);
internal Object const *eval_expr_prefix(Arena *arena, AstExprPrefix prefix, Env *env);
internal Object const *eval_expr_infix(Arena *arena, AstExprInfix infix, Env *env);
internal Object const *eval_expr_if_else(Arena *arena, AstExprIfElse if_else, Env *env);
internal Object const *eval_expr_call(Arena *arena, AstExprCall call, Env *env);
internal Object const *eval_expr_array(Arena *arena, AstExprArray array, Env *env);
internal Object const *eval_expr_hash(Arena *arena, AstExprHash hash, Env *env);
internal Object const *eval_expr_index(Arena *arena, AstExprIndex index, Env *env);

// =============================================================
// builtins

internal void env_builtin_init(Arena *arena);

#endif // CMONKEY_EVAL_H
