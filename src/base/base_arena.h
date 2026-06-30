#ifndef CMONKEY_BASE_ARENA_H
#define CMONKEY_BASE_ARENA_H

#include "base_core.h"

#define ARENA_DEFAULT_SIZE MiB(2)

typedef enum
{
  ArenaOpt_None   = 0,
  ArenaOpt_NoZero = 1 << 0,
} ArenaOpt;

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

typedef struct TmpArena TmpArena;
struct TmpArena
{
  Arena *arena;
  ArenaBlock *block;
  usize pos;
};

internal ArenaBlock *arena_block_create(usize size);

internal Arena *arena_create(usize size);
internal void *arena_alloc(Arena *arena, usize size, usize align, ArenaOpt opts);
internal void *
arena_realloc(Arena *arena, void *ptr, usize old_size, usize new_size, usize align, ArenaOpt opts);
internal void arena_free(Arena *arena);

#define arena_alloc_tn(arena, T, n) \
  (T *)arena_alloc((arena), sizeof(T) * (n), _Alignof(T), ArenaOpt_None)
#define arena_alloc_tn_nz(arena, T, n) \
  (T *)arena_alloc((arena), sizeof(T) * (n), _Alignof(T), ArenaOpt_NoZero)

#define arena_alloc_t(arena, T)    arena_alloc_tn((arena), T, 1)
#define arena_alloc_t_nz(arena, T) arena_alloc_tn_nz((arena), T, 1)

#define arena_realloc_t(arena, ptr, T, old_n, new_n) \
  (T *)arena_realloc((arena), (ptr), sizeof(T) * (old_n), sizeof(T) * (new_n), _Alignof(T), ArenaOpt_None)
#define arena_realloc_t_nz(arena, ptr, T, old_n, new_n) \
  (T *)arena_realloc((arena), (ptr), sizeof(T) * (old_n), sizeof(T) * (new_n), _Alignof(T), ArenaOpt_NoZero)

internal TmpArena arena_tmp_begin(Arena *arena);
internal void arena_tmp_commit(TmpArena *tmp);
internal void arena_tmp_end(TmpArena tmp);

internal Arena *tl_scratch_arena_get(Arena **conflicts, usize conflict_count);

#define scratch_begin(...)                                         \
  arena_tmp_begin(tl_scratch_arena_get((Arena *[]){ __VA_ARGS__ }, \
                                       sizeof((Arena *[]){ __VA_ARGS__ }) / sizeof(Arena *)))
#define scratch_end(scratch) arena_tmp_end(scratch)

#endif // CMONKEY_BASE_ARENA_H
