/**
 * intrusive_list_pool.h - Memory Pool for Cache-Friendly Intrusive Lists
 * 
 * The real performance win for linked lists isn't prefetching - it's
 * memory layout. When nodes are scattered across the heap, you get:
 *   - Cache misses on every node access
 *   - TLB misses across page boundaries  
 *   - Memory prefetcher can't help (random addresses)
 * 
 * A memory pool allocates nodes contiguously, giving you:
 *   - Sequential memory access during iteration
 *   - Hardware prefetcher works automatically
 *   - Single TLB entry covers many nodes
 *   - Cache lines contain multiple nodes
 * 
 * This typically provides 2-10x speedup for iteration-heavy workloads.
 * 
 * Author: Jesse (with Claude)
 * License: Public Domain / MIT
 */

#ifndef INTRUSIVE_LIST_POOL_H
#define INTRUSIVE_LIST_POOL_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "intrusive_list_fast.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Memory Pool Structure
 * ============================================================================ */

/**
 * Pool chunk - a block of contiguous memory for objects
 */
typedef struct ilist_pool_chunk {
    struct ilist_pool_chunk *next;  // Link to next chunk
    size_t capacity;                 // Objects in this chunk
    size_t used;                     // Objects allocated
    // Object storage follows immediately (flexible array member)
    char data[];
} ilist_pool_chunk_t;

/**
 * Memory pool with free list
 */
typedef struct ilist_pool {
    size_t object_size;              // Size of each object (aligned)
    size_t objects_per_chunk;        // Objects per allocation
    ilist_pool_chunk_t *chunks;      // Linked list of chunks
    ilist_pool_chunk_t *current;     // Current chunk for allocation
    ilist_node_t free_list;          // Free list head (reuses ilist_node_t!)
    size_t total_allocated;          // Stats: total objects allocated
    size_t total_freed;              // Stats: total objects freed
} ilist_pool_t;

/* ============================================================================
 * Pool Configuration
 * ============================================================================ */

#ifndef ILIST_POOL_DEFAULT_CHUNK_SIZE
#define ILIST_POOL_DEFAULT_CHUNK_SIZE 4096  // Objects per chunk
#endif

#ifndef ILIST_POOL_ALIGNMENT
#define ILIST_POOL_ALIGNMENT 16  // Alignment for objects
#endif

// Align size up to alignment boundary
#define ILIST_POOL_ALIGN(size, align) \
    (((size) + (align) - 1) & ~((align) - 1))

/* ============================================================================
 * Pool Lifecycle
 * ============================================================================ */

/**
 * ilist_pool_init - Initialize a memory pool
 * @pool:             Pool to initialize
 * @object_size:      Size of objects to allocate (must include ilist_node_t!)
 * @objects_per_chunk: Objects per chunk (0 = default)
 * 
 * Returns true on success, false on allocation failure.
 */
static inline bool ilist_pool_init(
    ilist_pool_t *pool,
    size_t object_size,
    size_t objects_per_chunk
) {
    if (objects_per_chunk == 0) {
        objects_per_chunk = ILIST_POOL_DEFAULT_CHUNK_SIZE;
    }
    
    // Ensure object size is aligned
    size_t aligned_size = ILIST_POOL_ALIGN(object_size, ILIST_POOL_ALIGNMENT);
    
    pool->object_size = aligned_size;
    pool->objects_per_chunk = objects_per_chunk;
    pool->chunks = NULL;
    pool->current = NULL;
    pool->total_allocated = 0;
    pool->total_freed = 0;
    
    // Initialize free list as empty circular list
    ilist_init_node_self(&pool->free_list);
    
    return true;
}

/**
 * ilist_pool_destroy - Free all pool memory
 * @pool: Pool to destroy
 */
static inline void ilist_pool_destroy(ilist_pool_t *pool) {
    ilist_pool_chunk_t *chunk = pool->chunks;
    while (chunk) {
        ilist_pool_chunk_t *next = chunk->next;
        free(chunk);
        chunk = next;
    }
    pool->chunks = NULL;
    pool->current = NULL;
    ilist_init_node_self(&pool->free_list);
}

/* ============================================================================
 * Internal: Chunk Management
 * ============================================================================ */

static inline ilist_pool_chunk_t *__ilist_pool_alloc_chunk(ilist_pool_t *pool) {
    size_t data_size = pool->object_size * pool->objects_per_chunk;
    size_t total_size = sizeof(ilist_pool_chunk_t) + data_size;
    
    ilist_pool_chunk_t *chunk = (ilist_pool_chunk_t *)malloc(total_size);
    if (!chunk) return NULL;
    
    chunk->next = pool->chunks;
    chunk->capacity = pool->objects_per_chunk;
    chunk->used = 0;
    
    pool->chunks = chunk;
    pool->current = chunk;
    
    return chunk;
}

/* ============================================================================
 * Allocation / Deallocation
 * ============================================================================ */

/**
 * ilist_pool_alloc - Allocate an object from the pool
 * @pool: Memory pool
 * 
 * Returns pointer to object, or NULL if out of memory.
 * Memory is NOT zeroed (use ilist_pool_calloc for that).
 */
static inline void *ilist_pool_alloc(ilist_pool_t *pool) {
    // First check free list
    if (pool->free_list.next != &pool->free_list) {
        ilist_node_t *node = pool->free_list.next;
        ilist_remove(node);
        pool->total_allocated++;
        return (void *)node;  // Object starts at node address
    }
    
    // Need to allocate from chunk
    ilist_pool_chunk_t *chunk = pool->current;
    
    // Need new chunk?
    if (!chunk || chunk->used >= chunk->capacity) {
        chunk = __ilist_pool_alloc_chunk(pool);
        if (!chunk) return NULL;
    }
    
    // Allocate from chunk
    void *obj = chunk->data + (chunk->used * pool->object_size);
    chunk->used++;
    pool->total_allocated++;
    
    return obj;
}

/**
 * ilist_pool_calloc - Allocate zeroed object from pool
 * @pool: Memory pool
 */
static inline void *ilist_pool_calloc(ilist_pool_t *pool) {
    void *obj = ilist_pool_alloc(pool);
    if (obj) {
        memset(obj, 0, pool->object_size);
    }
    return obj;
}

/**
 * ilist_pool_free - Return object to pool
 * @pool: Memory pool
 * @obj:  Object to free
 * 
 * The object is added to the free list for reuse.
 * The object MUST have an ilist_node_t at offset 0 or at a known offset.
 */
static inline void ilist_pool_free(ilist_pool_t *pool, void *obj) {
    if (!obj) return;
    
    // Reuse the object's memory as a free list node
    // This requires the object to have space for ilist_node_t
    ilist_node_t *node = (ilist_node_t *)obj;
    ilist_insert_after(&pool->free_list, node);
    pool->total_freed++;
}

/**
 * ilist_pool_free_node - Free using the node pointer directly
 * @pool:   Memory pool
 * @node:   The embedded ilist_node_t
 * @offset: Offset of node within object (usually offsetof(type, member))
 */
static inline void ilist_pool_free_node(
    ilist_pool_t *pool,
    ilist_node_t *node,
    size_t offset
) {
    void *obj = (char *)node - offset;
    ilist_pool_free(pool, obj);
}

/* ============================================================================
 * Pool Statistics
 * ============================================================================ */

static inline size_t ilist_pool_count_allocated(const ilist_pool_t *pool) {
    return pool->total_allocated - pool->total_freed;
}

static inline size_t ilist_pool_count_chunks(const ilist_pool_t *pool) {
    size_t count = 0;
    for (ilist_pool_chunk_t *c = pool->chunks; c; c = c->next) {
        count++;
    }
    return count;
}

static inline size_t ilist_pool_memory_used(const ilist_pool_t *pool) {
    size_t total = 0;
    for (ilist_pool_chunk_t *c = pool->chunks; c; c = c->next) {
        total += sizeof(ilist_pool_chunk_t) + (pool->object_size * c->capacity);
    }
    return total;
}

/* ============================================================================
 * Convenience Macro for Type-Safe Pool Access
 * ============================================================================ */

/**
 * ILIST_POOL_ALLOC - Type-safe allocation
 * @pool: Pool pointer
 * @type: Object type
 */
#define ILIST_POOL_ALLOC(pool, type) \
    ((type *)ilist_pool_alloc(pool))

#define ILIST_POOL_CALLOC(pool, type) \
    ((type *)ilist_pool_calloc(pool))

/**
 * ILIST_POOL_FREE_ENTRY - Free an object given its node
 * @pool:   Pool pointer
 * @node:   Pointer to the ilist_node_t member
 * @type:   Object type
 * @member: Name of the ilist_node_t member
 */
#define ILIST_POOL_FREE_ENTRY(pool, node, type, member) \
    ilist_pool_free_node(pool, node, offsetof(type, member))


#ifdef __cplusplus
}
#endif

#endif /* INTRUSIVE_LIST_POOL_H */
