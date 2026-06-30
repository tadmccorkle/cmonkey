#include "eval.h"

// =============================================================
// environment

internal Env *env_builtin = &(Env){ 0 };

internal bool
env_is_empty(EnvVar var)
{
  return var.name.buf == 0;
}

internal u32
env_hash(Str8 key)
{
  u32 hash = 2166136261;
  for (usize i = 0; i < key.len; i++)
  {
    hash ^= (u32)key.buf[i];
    hash *= 16777619;
  }
  return hash;
}

internal void
env_init(Env *env, Arena *arena, u32 initial_capacity)
{
  u32 cap = max(initial_capacity, 16);

  env->arena = arena;
  env->outer = 0;
  env->cnt   = 0;
  env->cap   = cap;
  env->vars  = arena_alloc_tn(arena, EnvVar, cap);
}

internal EnvVar *
env_find(Env const *env, Str8 name)
{
  u32 cap = env->cap;
  u32 h   = env_hash(name) % cap;

  for (;;)
  {
    EnvVar *var = &env->vars[h];

    if (env_is_empty(*var) || str8_equal(var->name, name))
    {
      return var;
    }

    h = (h + 1) % cap;
  }
}

internal void
env_grow(Env *env)
{
  TmpArena scratch = scratch_begin(env->arena);

  u32 old_cap = env->cap;
  u32 new_cap = old_cap * 2;

  EnvVar *old_vars = arena_alloc_tn_nz(scratch.arena, EnvVar, old_cap);
  memcpy(old_vars, env->vars, old_cap * sizeof(*env->vars));

  EnvVar *new_vars = arena_realloc_t_nz(env->arena, env->vars, EnvVar, env->cap, new_cap);
  memset(new_vars, 0, new_cap * sizeof(*env->vars));

  env->vars = new_vars;
  env->cap  = new_cap;

  for (usize i = 0; i < old_cap; i++)
  {
    EnvVar old_var = old_vars[i];
    if (!env_is_empty(old_var))
    {
      EnvVar *var = env_find(env, old_var.name);
      *var        = old_var;
    }
  }

  scratch_end(scratch);
}

internal Object const *
env_get(Env const *env, Str8 name)
{
  Object const *value;
  do
  {
    value = env_find(env, name)->value;
    if (value != 0) break;
    env = env->outer;
  } while (env != 0);
  return value;
}

internal void
env_set(Env *env, Str8 name, Object const *value)
{
  if (env->cnt + 1 > env->cap * ENV_LOAD_FACTOR)
  {
    env_grow(env);
  }

  EnvVar *var = env_find(env, name);
  if (env_is_empty(*var))
  {
    env->cnt += 1;
  }

  var->name  = name;
  var->value = value;
}

// =============================================================
// objects

internal Str8
object_name(ObjectKind kind)
{
  switch (kind)
  {
#define OBJECT_KIND(name) \
  case ObjectKind_##name: return str8_lit(#name);
    OBJECT_KINDS(OBJECT_KIND)
#undef OBJECT_KIND
    default: return str8_lit("Unknown Object Kind");
  }
}

internal Object const *
object_from_number(Arena *arena, s64 value)
{
  Object *result            = arena_alloc_t(arena, Object);
  result->tag               = ObjectKind_Number;
  result->data.number.value = value;
  return result;
}

internal Object const *
object_from_bool(bool value)
{
  return value ? OBJECT_TRUE : OBJECT_FALSE;
}

internal Object const *
object_from_string(Arena *arena, Str8 value)
{
  Object *result            = arena_alloc_t(arena, Object);
  result->tag               = ObjectKind_String;
  result->data.string.value = value;
  return result;
}

internal Object const *
object_from_array(Arena *arena, Object const **elements, usize len)
{
  Object *result              = arena_alloc_t(arena, Object);
  result->tag                 = ObjectKind_Array;
  result->data.array.elements = elements;
  result->data.array.len      = len;
  return result;
}

internal Object const *
object_from_function(Arena *arena, AstExprFunction fn, Env *env)
{
  Object *result               = arena_alloc_t(arena, Object);
  result->tag                  = ObjectKind_Function;
  result->data.function.params = fn.params;
  result->data.function.body   = fn.body;
  result->data.function.env    = env;
  return result;
}

internal Object const *
object_from_error(Arena *arena, Str8 error)
{
  Object *result         = arena_alloc_t(arena, Object);
  result->tag            = ObjectKind_Error;
  result->data.err.value = error;
  return result;
}

internal bool
object_is_truthy(Object const *object)
{
  return object->tag != ObjectKind_Null &&
         (object->tag != ObjectKind_Boolean || object->data.boolean.value);
}

internal bool
object_is_error(Object const *object)
{
  return object->tag == ObjectKind_Error;
}

// =============================================================
// hash objects

internal bool
object_hash_is_empty(ObjectHashPair p)
{
  return p.key == 0;
}

internal void
object_hash_init(ObjectHash *hash, Arena *arena, u32 initial_capacity)
{
  u32 cap = max(initial_capacity, 4);

  hash->cnt   = 0;
  hash->cap   = cap;
  hash->pairs = arena_alloc_tn(arena, ObjectHashPair, cap);
}

#define object_hash_init_count(hash, arena, initial_count) \
  object_hash_init((hash), (arena), ceil_pos(u32, (initial_count) / OBJECT_HASH_LOAD_FACTOR))

internal u32
object_hash(Object const *key)
{
  const u32 mult = 16777619;

  u32 hash = 2166136261;

  switch (key->tag)
  {
    case ObjectKind_String:
    {
      for (usize i = 0; i < key->data.string.value.len; i++)
      {
        hash ^= (u32)key->data.string.value.buf[i];
        hash *= mult;
      }
      break;
    }
    case ObjectKind_Number:
    {
      u64 n;
      memcpy(&n, &key->data.number.value, sizeof(u64));
      hash ^= (u32)(n ^ (n >> 32));
      hash *= mult;
      break;
    }
    case ObjectKind_Boolean:
    {
      hash ^= (u32)key->data.boolean.value;
      hash *= mult;
      break;
    }

    default: assert(0 && "Unsupported object hash type passed to hash function.");
  }

  return hash;
}

internal bool
object_hash_equal(Object const *a, Object const *b)
{
  if (a->tag != b->tag) return false;

  switch (a->tag)
  {
    case ObjectKind_String:
    {
      return str8_equal(a->data.string.value, b->data.string.value);
    }
    case ObjectKind_Number:
    {
      return a->data.number.value == b->data.number.value;
    }
    case ObjectKind_Boolean:
    {
      // NOTE(tad): Boolean and null objects should always reference singletons.
      return a == b;
    }

    default: assert(0 && "Unsupported object hash type passed to hash function.");
  }

  return false;
}

internal bool
object_hash_is_key_type(Object const *a)
{
  return a->tag == ObjectKind_String || a->tag == ObjectKind_Number || a->tag == ObjectKind_Boolean;
}

internal ObjectHashPair *
object_hash_find(ObjectHash const *hash, Object const *key)
{
  u32 cap = hash->cap;
  u32 h   = object_hash(key) % cap;

  for (;;)
  {
    ObjectHashPair *p = &hash->pairs[h];

    if (object_hash_is_empty(*p) || object_hash_equal(p->key, key))
    {
      return p;
    }

    h = (h + 1) % cap;
  }
}

internal void
object_hash_grow(Arena *arena, ObjectHash *hash)
{
  TmpArena scratch = scratch_begin(arena);

  u32 old_cap = hash->cap;
  u32 new_cap = old_cap * 2;

  ObjectHashPair *old_pairs = arena_alloc_tn_nz(scratch.arena, ObjectHashPair, old_cap);
  memcpy(old_pairs, hash->pairs, old_cap * sizeof(*hash->pairs));

  ObjectHashPair *new_pairs =
  arena_realloc_t_nz(arena, hash->pairs, ObjectHashPair, hash->cap, new_cap);
  memset(new_pairs, 0, new_cap * sizeof(*hash->pairs));

  hash->pairs = new_pairs;
  hash->cap   = new_cap;

  for (usize i = 0; i < old_cap; i++)
  {
    ObjectHashPair old_pair = old_pairs[i];
    if (!object_hash_is_empty(old_pair))
    {
      ObjectHashPair *p = object_hash_find(hash, old_pair.key);
      *p                = old_pair;
    }
  }

  scratch_end(scratch);
}

internal Object const *
object_hash_get(ObjectHash const *hash, Object const *key)
{
  Object const *value = object_hash_find(hash, key)->val;
  return value != 0 ? value : OBJECT_NULL;
}

internal void
object_hash_set(Arena *arena, ObjectHash *hash, Object const *key, Object const *val)
{
  if (hash->cnt + 1 > hash->cap * OBJECT_HASH_LOAD_FACTOR)
  {
    object_hash_grow(arena, hash);
  }

  ObjectHashPair *p = object_hash_find(hash, key);
  if (object_hash_is_empty(*p))
  {
    hash->cnt += 1;
  }

  p->key = key;
  p->val = val;
}

// =============================================================
// evaluation

internal Str8
eval_inspect(Arena *arena, Object const *o)
{
  switch (o->tag)
  {
    case ObjectKind_Number: return str8_f(arena, "%lld", o->data.number.value);
    case ObjectKind_Boolean: return o->data.boolean.value ? KEYWORD_TRUE : KEYWORD_FALSE;
    case ObjectKind_String: return str8_f(arena, "\"%.*s\"", str8_va(o->data.string.value));
    case ObjectKind_Return: return eval_inspect(arena, o->data.ret.value);
    case ObjectKind_Array:
    {
      StrBuilder8 b = str8_builder_create_default(arena);
      str8_append_lit(&b, "[");
      if (o->data.array.len > 0)
      {
        usize i = 0;
        for (; i < o->data.array.len - 1; i++)
        {
          str8_append(&b, eval_inspect(arena, o->data.array.elements[i]));
          str8_append_lit(&b, ", ");
        }
        str8_append(&b, eval_inspect(arena, o->data.array.elements[i]));
      }
      str8_append_lit(&b, "]");

      return str8_build(&b);
    }
    case ObjectKind_Hash:
    {
      StrBuilder8 b = str8_builder_create_default(arena);
      str8_append_lit(&b, "{");
      if (o->data.hash.cnt > 0)
      {
        usize inspected = 0;
        for (usize i = 0; i < o->data.hash.cap; i++)
        {
          ObjectHashPair p = o->data.hash.pairs[i];
          if (!object_hash_is_empty(p))
          {
            inspected++;

            str8_append(&b, eval_inspect(arena, o->data.hash.pairs[i].key));
            str8_append_lit(&b, ":");
            str8_append(&b, eval_inspect(arena, o->data.hash.pairs[i].val));

            if (inspected == o->data.hash.cnt) break;

            str8_append_lit(&b, ", ");
          }
        }
      }
      str8_append_lit(&b, "}");

      return str8_build(&b);
    }
    case ObjectKind_Function:
    {
      StrBuilder8 b = str8_builder_create_default(arena);
      str8_append_lit(&b, "fn(");
      if (o->data.function.params != 0)
      {
        AstExprFunctionParam *param = o->data.function.params;
        for (;;)
        {
          str8_append(&b, param->identifier.token.value);
          if ((param = param->next) == 0) break;
          str8_append_lit(&b, ", ");
        }
      }
      str8_append_lit(&b, ") { ");
      if (o->data.function.body->statements != 0)
      {
        parse_build_stmt_string(&b, o->data.function.body->statements);
        str8_append_lit(&b, " ");
      }
      str8_append_lit(&b, "}");

      return str8_build(&b);
    }
    case ObjectKind_Builtin:
    {
      StrBuilder8 b = str8_builder_create_default(arena);
      str8_append_lit(&b, "builtin:");
      str8_append(&b, o->data.builtin.name);
      return str8_build(&b);
    }
    case ObjectKind_Null: return str8_lit("");
    case ObjectKind_Error: return str8_f(arena, "Error: %.*s", str8_va(o->data.err.value));
    default:
      return str8_f(arena, "unsupported object inspection: %.*s", str8_va(object_name(o->tag)));
  }
}

internal Object const *
eval_expr_prefix(Arena *arena, AstExprPrefix prefix, Env *env)
{
  Object const *result;

  switch (prefix.token.kind)
  {
    case TokenKind_Bang:
    {
      Object const *rhs = eval_expr(arena, prefix.rhs, env);
      if (object_is_error(rhs))
      {
        result = object_from_error(arena, rhs->data.err.value);
      }
      else
      {
        result = object_from_bool(!object_is_truthy(rhs));
      }
      break;
    }

    case TokenKind_Minus:
    {
      Object const *rhs = eval_expr(arena, prefix.rhs, env);
      if (object_is_error(rhs))
      {
        result = object_from_error(arena, rhs->data.err.value);
      }
      else if (rhs->tag != ObjectKind_Number)
      {
        Str8 error = str8_f(arena, "unknown operator: -%.*s", str8_va(object_name(rhs->tag)));
        result     = object_from_error(arena, error);
      }
      else
      {
        result = object_from_number(arena, -rhs->data.number.value);
      }
      break;
    }

    default:
    {
      Str8 error = str8_f(arena,
                          "unknown operator: %.*s%.*s",
                          str8_va(prefix.token.value),
                          str8_va(ast_expr_name(prefix.rhs->tag)));
      result     = object_from_error(arena, error);
    }
  }

  return result;
}

internal Object const *
object_from_unknown_infix_op(Arena *arena, AstExprInfix infix, Object const *lhs, Object const *rhs)
{
  Str8 error = str8_f(arena,
                      "unknown operator: %.*s %.*s %.*s",
                      str8_va(object_name(lhs->tag)),
                      str8_va(infix.token.value),
                      str8_va(object_name(rhs->tag)));
  return object_from_error(arena, error);
}

internal Object const *
eval_expr_infix(Arena *arena, AstExprInfix infix, Env *env)
{
  Object const *result;

  Object const *lhs = eval_expr(arena, infix.lhs, env);
  Object const *rhs = eval_expr(arena, infix.rhs, env);
  TokenKind op      = infix.token.kind;

  if (object_is_error(lhs))
  {
    result = object_from_error(arena, lhs->data.err.value);
  }
  else if (object_is_error(rhs))
  {
    result = object_from_error(arena, rhs->data.err.value);
  }
  else if (lhs->tag == ObjectKind_Number && rhs->tag == ObjectKind_Number)
  {
    switch (op)
    {
      case TokenKind_Plus:
        result = object_from_number(arena, lhs->data.number.value + rhs->data.number.value);
        break;
      case TokenKind_Minus:
        result = object_from_number(arena, lhs->data.number.value - rhs->data.number.value);
        break;
      case TokenKind_Star:
        result = object_from_number(arena, lhs->data.number.value * rhs->data.number.value);
        break;
      case TokenKind_Slash:
        result = object_from_number(arena, lhs->data.number.value / rhs->data.number.value);
        break;
      case TokenKind_Less:
        result = object_from_bool(lhs->data.number.value < rhs->data.number.value);
        break;
      case TokenKind_Greater:
        result = object_from_bool(lhs->data.number.value > rhs->data.number.value);
        break;
      case TokenKind_Equal:
        result = object_from_bool(lhs->data.number.value == rhs->data.number.value);
        break;
      case TokenKind_NotEqual:
        result = object_from_bool(lhs->data.number.value != rhs->data.number.value);
        break;
      default: result = object_from_unknown_infix_op(arena, infix, lhs, rhs); break;
    }
  }
  else if (lhs->tag == ObjectKind_String && rhs->tag == ObjectKind_String)
  {
    switch (op)
    {
      case TokenKind_Plus:
      {
        Str8 value = str8_concat(arena, lhs->data.string.value, rhs->data.string.value);
        result     = object_from_string(arena, value);
        break;
      }
      case TokenKind_Equal:
        result = object_from_bool(str8_equal(lhs->data.string.value, rhs->data.string.value));
        break;
      case TokenKind_NotEqual:
        result = object_from_bool(!str8_equal(lhs->data.string.value, rhs->data.string.value));
        break;
      default: result = object_from_unknown_infix_op(arena, infix, lhs, rhs); break;
    }
  }
  else
  {
    switch (op)
    {
      // NOTE(tad): Boolean and null objects should always reference singletons.
      case TokenKind_Equal: result = object_from_bool(lhs == rhs); break;
      case TokenKind_NotEqual: result = object_from_bool(lhs != rhs); break;
      default:
      {
        if (lhs->tag == rhs->tag)
        {
          result = object_from_unknown_infix_op(arena, infix, lhs, rhs);
        }
        else
        {
          Str8 error = str8_f(arena,
                              "type mismatch: %.*s %.*s %.*s",
                              str8_va(object_name(lhs->tag)),
                              str8_va(infix.token.value),
                              str8_va(object_name(rhs->tag)));
          result     = object_from_error(arena, error);
        }
        break;
      }
    }
  }

  return result;
}

internal Object const *
eval_expr_if_else(Arena *arena, AstExprIfElse if_else, Env *env)
{
  Object const *condition = eval_expr(arena, if_else.condition, env);

  if (object_is_error(condition))
  {
    return condition;
  }
  if (object_is_truthy(condition))
  {
    return eval_block(arena, if_else.consequence, env);
  }
  if (if_else.alternative != 0)
  {
    return eval_block(arena, if_else.alternative, env);
  }

  return OBJECT_NULL;
}

internal Object const *
eval_call_function(Arena *arena, AstExprCall call, ObjectFunction call_fn, Env *env)
{
  // NOTE(tad): These arenas are currently the same, and neither of them are
  // scratch arenas. Still passing them both in the off chance a scratch arena
  // is later passed in for one of them. If both are different scratch arenas
  // will fail unless the number of scratch arenas is increased beyond 2.
  TmpArena scratch = scratch_begin(arena, env->arena, call_fn.env->arena);

  struct CallArgBinding
  {
    Str8 name;
    Object const *value;
  };

  struct CallArgBinding *args = arena_alloc_tn(scratch.arena, struct CallArgBinding, call.arg_count);

  u8 arg_index                     = 0;
  AstExprCallArg *arg_expr         = call.args;
  AstExprFunctionParam *param_expr = call_fn.params;
  while (arg_expr != 0 && param_expr != 0 && arg_index < call.arg_count)
  {
    Object const *arg_value = eval_expr(arena, arg_expr->expr, env);
    if (object_is_error(arg_value)) return arg_value;

    args[arg_index].name  = param_expr->identifier.token.value;
    args[arg_index].value = arg_value;

    arg_index += 1;
    arg_expr   = arg_expr->next;
    param_expr = param_expr->next;
  }

  if (param_expr != 0)
  {
    Str8 error =
    str8_f(arena, "missing function parameter: %.*s", str8_va(param_expr->identifier.token.value));
    return object_from_error(arena, error);
  }

  Env *extended_env = arena_alloc_t(arena, Env);
  env_init_count(extended_env, arena, call.arg_count);
  extended_env->outer = call_fn.env;

  for (u8 i = 0; i < arg_index; i++)
  {
    env_set(extended_env, args[i].name, args[i].value);
  }

  scratch_end(scratch);

  Object const *result = eval_block(arena, call_fn.body, extended_env);
  return result->tag == ObjectKind_Return ? result->data.ret.value : result;
}

internal Object const *
eval_call_builtin(Arena *arena, AstExprCall call, ObjectBuiltin call_builtin, Env *env)
{
  // NOTE(tad): These arenas are currently the same, and neither of them are
  // scratch arenas. Still passing them both in the off chance a scratch arena
  // is later passed in for one of them. If both are different scratch arenas
  // will fail unless the number of scratch arenas is increased beyond 2.
  TmpArena scratch = scratch_begin(arena, env->arena);

  Object const **args = arena_alloc_tn(scratch.arena, Object const *, call.arg_count);

  AstExprCallArg *arg = call.args;
  for (u8 i = 0; arg != 0 && i < call.arg_count; i++)
  {
    Object const *arg_value = eval_expr(arena, arg->expr, env);
    if (object_is_error(arg_value)) return arg_value;

    args[i] = arg_value;
    arg     = arg->next;
  }

  Object const *result = call_builtin.fn(arena, args, call.arg_count);

  scratch_end(scratch);

  return result;
}

internal Object const *
eval_expr_call(Arena *arena, AstExprCall call, Env *env)
{
  Object const *call_target = eval_expr(arena, call.function, env);
  if (object_is_error(call_target)) return call_target;

  if (call_target->tag == ObjectKind_Function)
  {
    return eval_call_function(arena, call, call_target->data.function, env);
  }
  else if (call_target->tag == ObjectKind_Builtin)
  {
    return eval_call_builtin(arena, call, call_target->data.builtin, env);
  }
  else
  {
    Str8 error = str8_f(arena, "not a function: %.*s", str8_va(object_name(call_target->tag)));
    return object_from_error(arena, error);
  }
}

internal Object const *
eval_expr_array(Arena *arena, AstExprArray array, Env *env)
{
  Object const **elements = array.count > 0 ? arena_alloc_tn(arena, Object const *, array.count) : 0;

  AstExprArrayElement *element_expr = array.elements;
  for (usize i = 0; element_expr != 0 && i < array.count; i++)
  {
    Object const *element = eval_expr(arena, element_expr->value, env);
    if (object_is_error(element)) return element;

    elements[i]  = element;
    element_expr = element_expr->next;
  }

  return object_from_array(arena, elements, array.count);
}

internal Object const *
eval_expr_hash(Arena *arena, AstExprHash hash, Env *env)
{
  Object *result = arena_alloc_t(arena, Object);
  result->tag    = ObjectKind_Hash;

  object_hash_init_count(&result->data.hash, arena, hash.count);

  AstExprHashPair *p = hash.pairs;
  for (usize i = 0; p != 0 && i < hash.count; p = p->next, i++)
  {
    Object const *k = eval_expr(arena, p->key, env);
    if (object_is_error(k)) return k;

    if (!object_hash_is_key_type(k))
    {
      Str8 error = str8_f(arena, "invalid hash key type: %.*s", str8_va(object_name(k->tag)));
      return object_from_error(arena, error);
    }

    Object const *v = eval_expr(arena, p->val, env);
    if (object_is_error(v)) return v;

    object_hash_set(arena, &result->data.hash, k, v);
  }

  return result;
}

internal Object const *
eval_index_array(ObjectArray array, ObjectNumber index)
{
  return index.value >= 0 && (usize)index.value < array.len ? array.elements[index.value] : OBJECT_NULL;
}

internal Object const *
eval_index_string(Arena *arena, ObjectString string, ObjectNumber index)
{
  return index.value >= 0 && (usize)index.value < string.value.len
       ? object_from_string(arena, str8(string.value.buf + index.value, 1))
       : OBJECT_NULL;
}

internal Object const *
eval_expr_index(Arena *arena, AstExprIndex index_expr, Env *env)
{
  Object const *target = eval_expr(arena, index_expr.lhs, env);
  if (object_is_error(target)) return target;

  Object const *index = eval_expr(arena, index_expr.index, env);
  if (object_is_error(index)) return index;

  switch (target->tag)
  {
    case ObjectKind_Array:
    {
      if (index->tag != ObjectKind_Number)
      {
        Str8 error = str8_f(arena, "invalid array index type: %.*s", str8_va(object_name(index->tag)));
        return object_from_error(arena, error);
      }

      return eval_index_array(target->data.array, index->data.number);
    }

    case ObjectKind_String:
    {
      if (index->tag != ObjectKind_Number)
      {
        Str8 error = str8_f(arena, "invalid string index type: %.*s", str8_va(object_name(index->tag)));
        return object_from_error(arena, error);
      }

      return eval_index_string(arena, target->data.string, index->data.number);
    }

    case ObjectKind_Hash:
    {
      if (!object_hash_is_key_type(index))
      {
        Str8 error = str8_f(arena, "invalid hash index type: %.*s", str8_va(object_name(index->tag)));
        return object_from_error(arena, error);
      }

      return object_hash_get(&target->data.hash, index);
    }

    default:
    {
      Str8 error =
      str8_f(arena, "indexing not supported for type: %.*s", str8_va(object_name(target->tag)));
      return object_from_error(arena, error);
    }
  }
}

internal Object const *
eval_expr(Arena *arena, AstExpr *expr, Env *env)
{
  switch (expr->tag)
  {
    case AstExpr_Identifier:
    {
      Str8 name           = expr->data.identifier.token.value;
      Object const *value = env_get(env, name);
      if (value == 0)
      {
        value = env_get(env_builtin, name);
        if (value == 0)
        {
          Str8 error = str8_f(arena, "identifier not found: %.*s", str8_va(name));
          value      = object_from_error(arena, error);
        }
      }
      return value;
    }
    case AstExpr_Number: return object_from_number(arena, expr->data.number.value);
    case AstExpr_Boolean: return object_from_bool(expr->data.boolean.value);
    case AstExpr_String: return object_from_string(arena, expr->data.string.value);
    case AstExpr_Function: return object_from_function(arena, expr->data.function, env);

    case AstExpr_Prefix: return eval_expr_prefix(arena, expr->data.prefix, env);
    case AstExpr_Infix: return eval_expr_infix(arena, expr->data.infix, env);
    case AstExpr_IfElse: return eval_expr_if_else(arena, expr->data.if_else, env);
    case AstExpr_Call: return eval_expr_call(arena, expr->data.call, env);
    case AstExpr_Array: return eval_expr_array(arena, expr->data.array, env);
    case AstExpr_Hash: return eval_expr_hash(arena, expr->data.hash, env);
    case AstExpr_Index: return eval_expr_index(arena, expr->data.index, env);

    default: return OBJECT_NULL;
  }
}

internal Object const *
eval_stmt(Arena *arena, AstStmt *stmt, Env *env)
{
  switch (stmt->tag)
  {
    case AstStmt_Let:
    {
      Object const *value = eval_expr(arena, stmt->data.let.expr, env);
      if (object_is_error(value)) return value;

      env_set(env, stmt->data.let.identifier->token.value, value);

      return OBJECT_NULL;
    }

    case AstStmt_Ret:
    {
      Object const *ret_value = eval_expr(arena, stmt->data.ret.expr, env);
      if (object_is_error(ret_value)) return ret_value;

      Object *ret         = arena_alloc_t(arena, Object);
      ret->tag            = ObjectKind_Return;
      ret->data.ret.value = ret_value;
      return ret;
    }

    case AstStmt_Expr: return eval_expr(arena, stmt->data.expr.expr, env);

    default: return OBJECT_NULL;
  }
}

internal Object const *
eval_block(Arena *arena, AstStmtBlock *block, Env *env)
{
  Object const *result = OBJECT_NULL;

  for (AstStmt *stmt = block->statements; stmt != 0; stmt = stmt->next)
  {
    result = eval_stmt(arena, stmt, env);

    if (result->tag == ObjectKind_Return) return result;
    if (result->tag == ObjectKind_Error) return result;
  }

  return result;
}

// TODO(tad): This doesn't do any sort of memory management yet. While some intermediate
// evaluations are freed after use (and this might actually introduce bugs), any objects
// saved to an environment and environments themselves live for the life of the program
// (or until the provided arena is freed).
//
// Consider a simple GC implementation. Maybe a free list built on top of arenas?
internal Object const *
eval_program(Arena *arena, AstStmt *statements, Env *env)
{
  Object const *result = OBJECT_NULL;

  for (AstStmt *stmt = statements; stmt != 0; stmt = stmt->next)
  {
    result = eval_stmt(arena, stmt, env);

    if (result->tag == ObjectKind_Return) return result->data.ret.value;
    if (result->tag == ObjectKind_Error) return result;
  }

  return result;
}

// =============================================================
// builtins

internal Object const *
eval_builtin_len(Arena *arena, Object const **args, u8 arg_count)
{
  if (arg_count != 1)
  {
    Str8 error = str8_f(arena, "builtin 'len' requires 1 argument, received: %u", arg_count);
    return object_from_error(arena, error);
  }

  Object const *arg = *args;
  switch (arg->tag)
  {
    case ObjectKind_String: return object_from_number(arena, (s64)arg->data.string.value.len);
    case ObjectKind_Array: return object_from_number(arena, (s64)arg->data.array.len);
    default:
    {
      Str8 error =
      str8_f(arena, "argument to builtin 'len' not supported: %.*s", str8_va(object_name(arg->tag)));
      return object_from_error(arena, error);
    }
  }
}

internal Object const *
eval_builtin_rest(Arena *arena, Object const **args, u8 arg_count)
{
  if (arg_count != 1)
  {
    Str8 error = str8_f(arena, "builtin 'rest' requires 1 argument, received: %u", arg_count);
    return object_from_error(arena, error);
  }

  Object const *arg = *args;

  if (arg->tag != ObjectKind_Array)
  {
    Str8 error =
    str8_f(arena, "argument to builtin 'rest' not supported: %.*s", str8_va(object_name(arg->tag)));
    return object_from_error(arena, error);
  }

  ObjectArray src = arg->data.array;

  if (src.len == 0)
  {
    return OBJECT_NULL;
  }

  usize len = src.len - 1;

  Object const **elements;
  if (len > 0)
  {
    elements = arena_alloc_tn(arena, Object const *, len);
    memcpy(elements, src.elements + 1, len * sizeof(Object const *));
  }
  else
  {
    elements = 0;
  }

  return object_from_array(arena, elements, len);
}

internal Object const *
eval_builtin_push(Arena *arena, Object const **args, u8 arg_count)
{
  if (arg_count != 2)
  {
    Str8 error = str8_f(arena, "builtin 'push' requires 2 argument, received: %u", arg_count);
    return object_from_error(arena, error);
  }

  Object const *arg1 = args[0];
  Object const *arg2 = args[1];

  if (arg1->tag != ObjectKind_Array)
  {
    Str8 error =
    str8_f(arena, "argument to builtin 'push' not supported: %.*s", str8_va(object_name(arg1->tag)));
    return object_from_error(arena, error);
  }

  ObjectArray src         = arg1->data.array;
  usize len               = src.len + 1;
  Object const **elements = arena_alloc_tn(arena, Object const *, len);
  elements[src.len]       = arg2;

  memcpy(elements, src.elements, src.len * sizeof(Object const *));

  return object_from_array(arena, elements, len);
}

internal Object const *
eval_builtin_puts(Arena *arena, Object const **args, u8 arg_count)
{
  if (arg_count > 0)
  {
    u8 i = 0;
    for (;;)
    {
      TmpArena scratch = scratch_begin(arena);
      Str8 output      = eval_inspect(scratch.arena, args[i]);
      if (args[i]->tag == ObjectKind_String)
      {
        output = str8_slice(output, 1, output.len - 1);
      }
      else if (args[i]->tag == ObjectKind_Null)
      {
        output = str8_lit("null");
      }
      printf("%.*s", str8_va(output));
      scratch_end(scratch);

      if (++i == arg_count) break;
      printf(" ");
    }
  }
  printf("\n");

  return OBJECT_NULL;
}

internal Object const *builtin_len = &(Object){ .tag               = ObjectKind_Builtin,
                                                .data.builtin.name = str8_lit("len"),
                                                .data.builtin.fn   = eval_builtin_len };

internal Object const *builtin_rest = &(Object){ .tag               = ObjectKind_Builtin,
                                                 .data.builtin.name = str8_lit("rest"),
                                                 .data.builtin.fn   = eval_builtin_rest };

internal Object const *builtin_push = &(Object){ .tag               = ObjectKind_Builtin,
                                                 .data.builtin.name = str8_lit("push"),
                                                 .data.builtin.fn   = eval_builtin_push };

internal Object const *builtin_puts = &(Object){ .tag               = ObjectKind_Builtin,
                                                 .data.builtin.name = str8_lit("puts"),
                                                 .data.builtin.fn   = eval_builtin_puts };

internal void
env_builtin_init(Arena *arena)
{
  env_init(env_builtin, arena, 0);
  env_set(env_builtin, builtin_len->data.builtin.name, builtin_len);
  env_set(env_builtin, builtin_rest->data.builtin.name, builtin_rest);
  env_set(env_builtin, builtin_push->data.builtin.name, builtin_push);
  env_set(env_builtin, builtin_puts->data.builtin.name, builtin_puts);
}
