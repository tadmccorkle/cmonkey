#include <stdlib.h>

#include "base_arena.h"

internal ArenaBlock *
arena_block_create(usize size)
{
  ArenaBlock *block = malloc(sizeof(ArenaBlock) + size);
  assert_m(block, "Allocation failure.");
  block->prev = 0;
  block->size = size;
  block->pos  = 0;
  return block;
}

internal Arena *
arena_create(usize size)
{
  Arena *arena = malloc(sizeof(Arena));
  assert_m(arena, "Allocation failure.");
  arena->current = arena_block_create(size);
  return arena;
}

internal void *
arena_alloc(Arena *arena, usize size, usize align, ArenaOpt opts)
{
  ArenaBlock *block = arena->current;
  usize pos         = align_up(block->pos, align);

  if (pos + size > block->size)
  {
    usize new_size = block->size * 2;
    if (size > new_size)
    {
      new_size = size;
    }

    // TODO(tad): replace printf with configurable log
    printf("Creating new arena block\n");
    ArenaBlock *new_block = arena_block_create(new_size);
    new_block->prev       = block;
    arena->current        = new_block;
    block                 = new_block;
    pos                   = 0;
  }

  void *result = block->data + pos;
  block->pos   = pos + size;

  if (!(opts & ArenaOpt_NoZero))
  {
    memset(result, 0, size);
  }

  return result;
}

internal void *
arena_realloc(Arena *arena, void *ptr, usize old_size, usize new_size, usize align, ArenaOpt opts)
{
  if (ptr == 0 || old_size == 0) return arena_alloc(arena, new_size, align, opts);

  ArenaBlock *block = arena->current;

  if ((u8 *)ptr + old_size == block->data + block->pos)
  {
    if (new_size <= old_size)
    {
      block->pos -= old_size - new_size;
      return ptr;
    }

    usize required = new_size - old_size;
    if (block->pos + required <= block->size)
    {
      if (!(opts & ArenaOpt_NoZero))
      {
        memset((u8 *)ptr + old_size, 0, required);
      }
      block->pos += required;
      return ptr;
    }
  }

  void *result = arena_alloc(arena, new_size, align, opts);
  memcpy(result, ptr, min(old_size, new_size));
  return result;
}

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
arena_tmp_commit(TmpArena *tmp)
{
  tmp->block = tmp->arena->current;
  tmp->pos   = tmp->arena->current->pos;
}

internal void
arena_tmp_end(TmpArena tmp)
{
  if (tmp.arena == 0) return;

  Arena *arena = tmp.arena;

  while (arena->current != tmp.block)
  {
    ArenaBlock *block = arena->current;
    arena->current    = block->prev;
    free(block);
  }

  arena->current->pos = tmp.pos;
}

#define SCRATCH_ARENA_COUNT 2

internal per_thread Arena *tl_scratch_arenas[SCRATCH_ARENA_COUNT];

internal Arena *
tl_scratch_arena_get(Arena **conflicts, usize conflict_count)
{
  if (!tl_scratch_arenas[0])
  {
    for (usize i = 0; i < SCRATCH_ARENA_COUNT; i++)
    {
      tl_scratch_arenas[i] = arena_create(ARENA_DEFAULT_SIZE);
    }
  }

  for (usize i = 0; i < SCRATCH_ARENA_COUNT; i++)
  {
    Arena *candidate = tl_scratch_arenas[i];

    bool is_conflict = false;
    for (usize j = 0; j < conflict_count; j++)
    {
      if (conflicts[j] == candidate)
      {
        is_conflict = true;
        break;
      }
    }

    if (!is_conflict) return candidate;
  }

  PANIC("No non-conflicting scratch arena available.");
}
