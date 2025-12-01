/**
 * intrusive_list_fast.h - Performance-Optimized Intrusive Doubly-Linked List
 * 
 * Optimizations over standard implementation:
 *   - Software prefetching during iteration (hides memory latency)
 *   - Branch prediction hints (likely/unlikely)
 *   - Forced inlining for critical paths
 *   - Branchless operations where beneficial
 *   - Batch operations (insert/remove multiple nodes)
 *   - Cache-line aware design considerations
 *   - Restrict pointers for alias analysis
 * 
 * Author: Jesse (with Claude)
 * License: Public Domain / MIT
 */

#ifndef INTRUSIVE_LIST_FAST_H
#define INTRUSIVE_LIST_FAST_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Compiler Intrinsics & Hints
 * ============================================================================ */

// Branch prediction hints
#if defined(__GNUC__) || defined(__clang__)
    #define ilist_likely(x)   __builtin_expect(!!(x), 1)
    #define ilist_unlikely(x) __builtin_expect(!!(x), 0)
#else
    #define ilist_likely(x)   (x)
    #define ilist_unlikely(x) (x)
#endif

// Force inlining (not just a suggestion)
#if defined(__GNUC__) || defined(__clang__)
    #define ILIST_FORCE_INLINE static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
    #define ILIST_FORCE_INLINE static __forceinline
#else
    #define ILIST_FORCE_INLINE static inline
#endif

// Prefetch hints
#if defined(__GNUC__) || defined(__clang__)
    // Locality hints: 0 = no temporal locality (use once), 3 = high locality (keep in cache)
    #define ilist_prefetch_read(addr)      __builtin_prefetch((addr), 0, 1)
    #define ilist_prefetch_write(addr)     __builtin_prefetch((addr), 1, 1)
    #define ilist_prefetch_read_high(addr) __builtin_prefetch((addr), 0, 3)
#else
    #define ilist_prefetch_read(addr)      ((void)(addr))
    #define ilist_prefetch_write(addr)     ((void)(addr))
    #define ilist_prefetch_read_high(addr) ((void)(addr))
#endif

// Restrict pointer (no aliasing)
#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
    #define ILIST_RESTRICT __restrict
#else
    #define ILIST_RESTRICT
#endif

// Cache line size (common default, could be runtime detected)
#ifndef ILIST_CACHE_LINE_SIZE
    #define ILIST_CACHE_LINE_SIZE 64
#endif

// Alignment hint for structures
#if defined(__GNUC__) || defined(__clang__)
    #define ILIST_CACHE_ALIGNED __attribute__((aligned(ILIST_CACHE_LINE_SIZE)))
#else
    #define ILIST_CACHE_ALIGNED
#endif


/* ============================================================================
 * Core Structures
 * ============================================================================ */

typedef struct ilist_node {
    struct ilist_node *next;
    struct ilist_node *prev;
} ilist_node_t;

typedef struct ilist_head {
    ilist_node_t node;
} ilist_head_t;

// Cache-aligned version for hot lists
typedef struct ilist_head_aligned {
    ilist_node_t node;
} ILIST_CACHE_ALIGNED ilist_head_aligned_t;


/* ============================================================================
 * Utility Macros
 * ============================================================================ */

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

#define ilist_entry(ptr, type, member) \
    container_of(ptr, type, member)

#define ilist_entry_safe(ptr, type, member) \
    ((ptr) ? ilist_entry(ptr, type, member) : NULL)

#define ilist_first_entry(head, type, member) \
    ilist_entry((head)->node.next, type, member)

#define ilist_last_entry(head, type, member) \
    ilist_entry((head)->node.prev, type, member)

#define ilist_first_entry_or_null(head, type, member) \
    (ilist_likely(!ilist_is_empty(head)) ? ilist_first_entry(head, type, member) : NULL)

#define ilist_last_entry_or_null(head, type, member) \
    (ilist_likely(!ilist_is_empty(head)) ? ilist_last_entry(head, type, member) : NULL)

#define ilist_next_entry(pos, member) \
    ilist_entry((pos)->member.next, typeof(*(pos)), member)

#define ilist_prev_entry(pos, member) \
    ilist_entry((pos)->member.prev, typeof(*(pos)), member)


/* ============================================================================
 * Initialization
 * ============================================================================ */

#define ILIST_HEAD_INIT(name) { .node = { .next = &(name).node, .prev = &(name).node } }
#define ILIST_HEAD(name) ilist_head_t name = ILIST_HEAD_INIT(name)
#define ILIST_NODE_INIT { .next = NULL, .prev = NULL }

ILIST_FORCE_INLINE void ilist_init_head(ilist_head_t *head) {
    head->node.next = &head->node;
    head->node.prev = &head->node;
}

ILIST_FORCE_INLINE void ilist_init_node(ilist_node_t *node) {
    node->next = NULL;
    node->prev = NULL;
}

ILIST_FORCE_INLINE void ilist_init_node_self(ilist_node_t *node) {
    node->next = node;
    node->prev = node;
}


/* ============================================================================
 * State Queries (Branchless where possible)
 * ============================================================================ */

ILIST_FORCE_INLINE bool ilist_is_empty(const ilist_head_t *head) {
    return head->node.next == &head->node;
}

ILIST_FORCE_INLINE bool ilist_is_singular(const ilist_head_t *head) {
    const ilist_node_t *next = head->node.next;
    // Branchless: not empty AND next equals prev
    return (next != &head->node) & (next == head->node.prev);
}

ILIST_FORCE_INLINE bool ilist_is_linked(const ilist_node_t *node) {
    return node->next != NULL;
}

ILIST_FORCE_INLINE bool ilist_is_first(const ilist_node_t *node, const ilist_head_t *head) {
    return node->prev == &head->node;
}

ILIST_FORCE_INLINE bool ilist_is_last(const ilist_node_t *node, const ilist_head_t *head) {
    return node->next == &head->node;
}

ILIST_FORCE_INLINE bool ilist_is_head(const ilist_node_t *node, const ilist_head_t *head) {
    return node == &head->node;
}


/* ============================================================================
 * Core Operations (Optimized)
 * ============================================================================ */

/**
 * __ilist_insert - Insert node between two nodes
 * 
 * Uses restrict to tell compiler prev/next don't alias,
 * enabling better instruction scheduling.
 */
ILIST_FORCE_INLINE void __ilist_insert(
    ilist_node_t * ILIST_RESTRICT node,
    ilist_node_t * ILIST_RESTRICT prev,
    ilist_node_t * ILIST_RESTRICT next
) {
    // Write new node's pointers first (may execute in parallel)
    node->next = next;
    node->prev = prev;
    // Then update neighbors (these have data dependency on each other through node)
    // Compiler can reorder these two since they're independent
    prev->next = node;
    next->prev = node;
}

ILIST_FORCE_INLINE void __ilist_remove(
    ilist_node_t * ILIST_RESTRICT prev,
    ilist_node_t * ILIST_RESTRICT next
) {
    next->prev = prev;
    prev->next = next;
}


/* ============================================================================
 * Insertion Operations
 * ============================================================================ */

ILIST_FORCE_INLINE void ilist_insert_after(ilist_node_t *ref, ilist_node_t *node) {
    __ilist_insert(node, ref, ref->next);
}

ILIST_FORCE_INLINE void ilist_insert_before(ilist_node_t *ref, ilist_node_t *node) {
    __ilist_insert(node, ref->prev, ref);
}

ILIST_FORCE_INLINE void ilist_push_front(ilist_head_t *head, ilist_node_t *node) {
    __ilist_insert(node, &head->node, head->node.next);
}

ILIST_FORCE_INLINE void ilist_push_back(ilist_head_t *head, ilist_node_t *node) {
    __ilist_insert(node, head->node.prev, &head->node);
}


/* ============================================================================
 * Batch Insertion (Optimized for multiple inserts)
 * ============================================================================ */

/**
 * ilist_push_back_batch - Insert array of nodes at back
 * @head:  List head
 * @nodes: Array of node pointers
 * @count: Number of nodes to insert
 * 
 * More efficient than individual push_back calls because:
 * 1. Single fetch of head->node.prev
 * 2. Predictable memory access pattern for prefetching
 * 3. Reduced function call overhead in tight loops
 */
ILIST_FORCE_INLINE void ilist_push_back_batch(
    ilist_head_t *head,
    ilist_node_t **nodes,
    size_t count
) {
    if (ilist_unlikely(count == 0)) return;
    
    ilist_node_t *tail = head->node.prev;
    ilist_node_t *sentinel = &head->node;
    
    // Prefetch first few nodes
    if (count > 1) ilist_prefetch_write(nodes[1]);
    if (count > 2) ilist_prefetch_write(nodes[2]);
    
    for (size_t i = 0; i < count; i++) {
        // Prefetch ahead (4 nodes ahead is typical sweet spot)
        if (i + 4 < count) {
            ilist_prefetch_write(nodes[i + 4]);
        }
        
        ilist_node_t *node = nodes[i];
        node->prev = tail;
        node->next = sentinel;
        tail->next = node;
        tail = node;
    }
    
    sentinel->prev = tail;
}

/**
 * ilist_push_front_batch - Insert array of nodes at front (maintains order)
 * @head:  List head
 * @nodes: Array of node pointers
 * @count: Number of nodes
 * 
 * After insertion, nodes[0] will be first, nodes[count-1] last of the batch.
 */
ILIST_FORCE_INLINE void ilist_push_front_batch(
    ilist_head_t *head,
    ilist_node_t **nodes,
    size_t count
) {
    if (ilist_unlikely(count == 0)) return;
    
    ilist_node_t *old_first = head->node.next;
    ilist_node_t *sentinel = &head->node;
    
    // Link nodes together in a chain first
    ilist_node_t *prev = sentinel;
    for (size_t i = 0; i < count; i++) {
        if (i + 4 < count) {
            ilist_prefetch_write(nodes[i + 4]);
        }
        
        ilist_node_t *node = nodes[i];
        node->prev = prev;
        prev->next = node;
        prev = node;
    }
    
    // Connect end of batch to old first element
    prev->next = old_first;
    old_first->prev = prev;
}


/* ============================================================================
 * Removal Operations
 * ============================================================================ */

ILIST_FORCE_INLINE void ilist_remove(ilist_node_t *node) {
    __ilist_remove(node->prev, node->next);
}

ILIST_FORCE_INLINE void ilist_remove_init(ilist_node_t *node) {
    __ilist_remove(node->prev, node->next);
    ilist_init_node(node);
}

ILIST_FORCE_INLINE ilist_node_t *ilist_pop_front(ilist_head_t *head) {
    if (ilist_unlikely(ilist_is_empty(head))) {
        return NULL;
    }
    ilist_node_t *node = head->node.next;
    ilist_remove(node);
    return node;
}

ILIST_FORCE_INLINE ilist_node_t *ilist_pop_back(ilist_head_t *head) {
    if (ilist_unlikely(ilist_is_empty(head))) {
        return NULL;
    }
    ilist_node_t *node = head->node.prev;
    ilist_remove(node);
    return node;
}

/**
 * ilist_pop_front_batch - Remove multiple nodes from front
 * @head:  List head
 * @out:   Output array for removed nodes
 * @count: Maximum nodes to remove
 * 
 * Returns actual number of nodes removed (may be less than count).
 */
ILIST_FORCE_INLINE size_t ilist_pop_front_batch(
    ilist_head_t *head,
    ilist_node_t **out,
    size_t count
) {
    ilist_node_t *sentinel = &head->node;
    ilist_node_t *current = sentinel->next;
    size_t removed = 0;
    
    while (removed < count && current != sentinel) {
        // Prefetch next node while processing current
        ilist_prefetch_read(current->next);
        
        out[removed] = current;
        current = current->next;
        removed++;
    }
    
    if (ilist_likely(removed > 0)) {
        // Relink: sentinel <-> current
        sentinel->next = current;
        current->prev = sentinel;
    }
    
    return removed;
}


/* ============================================================================
 * Movement Operations
 * ============================================================================ */

ILIST_FORCE_INLINE void ilist_move_to_front(ilist_head_t *head, ilist_node_t *node) {
    __ilist_remove(node->prev, node->next);
    __ilist_insert(node, &head->node, head->node.next);
}

ILIST_FORCE_INLINE void ilist_move_to_back(ilist_head_t *head, ilist_node_t *node) {
    __ilist_remove(node->prev, node->next);
    __ilist_insert(node, head->node.prev, &head->node);
}

ILIST_FORCE_INLINE void ilist_replace(ilist_node_t *old_node, ilist_node_t *new_node) {
    new_node->next = old_node->next;
    new_node->prev = old_node->prev;
    new_node->prev->next = new_node;
    new_node->next->prev = new_node;
}

ILIST_FORCE_INLINE void ilist_replace_init(ilist_node_t *old_node, ilist_node_t *new_node) {
    ilist_replace(old_node, new_node);
    ilist_init_node(old_node);
}

ILIST_FORCE_INLINE void ilist_swap(ilist_node_t *a, ilist_node_t *b) {
    if (ilist_unlikely(a == b)) return;
    
    ilist_node_t *a_prev = a->prev;
    ilist_node_t *a_next = a->next;
    ilist_node_t *b_prev = b->prev;
    ilist_node_t *b_next = b->next;
    
    // Handle adjacent case with branchless selection
    bool adjacent_ab = (a->next == b);
    bool adjacent_ba = (b->next == a);
    
    if (ilist_unlikely(adjacent_ab)) {
        a->next = b_next;
        a->prev = b;
        b->next = a;
        b->prev = a_prev;
        a_prev->next = b;
        b_next->prev = a;
    } else if (ilist_unlikely(adjacent_ba)) {
        b->next = a_next;
        b->prev = a;
        a->next = b;
        a->prev = b_prev;
        b_prev->next = a;
        a_next->prev = b;
    } else {
        // Non-adjacent - most common case
        a->prev = b_prev;
        a->next = b_next;
        b->prev = a_prev;
        b->next = a_next;
        
        a_prev->next = b;
        a_next->prev = b;
        b_prev->next = a;
        b_next->prev = a;
    }
}


/* ============================================================================
 * Splice Operations
 * ============================================================================ */

ILIST_FORCE_INLINE void __ilist_splice(
    const ilist_head_t *list,
    ilist_node_t *prev,
    ilist_node_t *next
) {
    ilist_node_t *first = list->node.next;
    ilist_node_t *last = list->node.prev;
    
    first->prev = prev;
    prev->next = first;
    last->next = next;
    next->prev = last;
}

ILIST_FORCE_INLINE void ilist_splice_front(ilist_head_t *src, ilist_head_t *dst) {
    if (ilist_likely(!ilist_is_empty(src))) {
        __ilist_splice(src, &dst->node, dst->node.next);
        ilist_init_head(src);
    }
}

ILIST_FORCE_INLINE void ilist_splice_back(ilist_head_t *src, ilist_head_t *dst) {
    if (ilist_likely(!ilist_is_empty(src))) {
        __ilist_splice(src, dst->node.prev, &dst->node);
        ilist_init_head(src);
    }
}

ILIST_FORCE_INLINE void ilist_cut_before(
    ilist_head_t *head,
    ilist_head_t *new_head,
    ilist_node_t *node
) {
    if (ilist_unlikely(ilist_is_empty(head) || node == head->node.next)) {
        ilist_init_head(new_head);
        return;
    }
    
    new_head->node.next = head->node.next;
    new_head->node.next->prev = &new_head->node;
    new_head->node.prev = node->prev;
    new_head->node.prev->next = &new_head->node;
    
    head->node.next = node;
    node->prev = &head->node;
}


/* ============================================================================
 * Rotation
 * ============================================================================ */

ILIST_FORCE_INLINE void ilist_rotate_left(ilist_head_t *head) {
    if (ilist_likely(!ilist_is_empty(head))) {
        ilist_move_to_back(head, head->node.next);
    }
}

ILIST_FORCE_INLINE void ilist_rotate_right(ilist_head_t *head) {
    if (ilist_likely(!ilist_is_empty(head))) {
        ilist_move_to_front(head, head->node.prev);
    }
}


/* ============================================================================
 * Iteration Macros - Standard
 * ============================================================================ */

#define ilist_foreach(pos, head) \
    for (pos = (head)->node.next; \
         !ilist_is_head(pos, head); \
         pos = pos->next)

#define ilist_foreach_reverse(pos, head) \
    for (pos = (head)->node.prev; \
         !ilist_is_head(pos, head); \
         pos = pos->prev)

#define ilist_foreach_safe(pos, tmp, head) \
    for (pos = (head)->node.next, tmp = pos->next; \
         !ilist_is_head(pos, head); \
         pos = tmp, tmp = pos->next)

#define ilist_foreach_reverse_safe(pos, tmp, head) \
    for (pos = (head)->node.prev, tmp = pos->prev; \
         !ilist_is_head(pos, head); \
         pos = tmp, tmp = pos->prev)


/* ============================================================================
 * Iteration Macros - With Prefetching (The Performance Magic)
 * ============================================================================ */

/**
 * ilist_foreach_prefetch - Iterate with software prefetching
 * 
 * Prefetches the next node while processing current, hiding memory latency.
 * On modern CPUs with 200+ cycle memory latency, this can be huge.
 */
#define ilist_foreach_prefetch(pos, head) \
    for (pos = (head)->node.next; \
         !ilist_is_head(pos, head) && (ilist_prefetch_read(pos->next), 1); \
         pos = pos->next)

#define ilist_foreach_reverse_prefetch(pos, head) \
    for (pos = (head)->node.prev; \
         !ilist_is_head(pos, head) && (ilist_prefetch_read(pos->prev), 1); \
         pos = pos->prev)

#define ilist_foreach_safe_prefetch(pos, tmp, head) \
    for (pos = (head)->node.next, tmp = pos->next; \
         !ilist_is_head(pos, head) && (ilist_prefetch_read(tmp->next), 1); \
         pos = tmp, tmp = pos->next)

/**
 * ilist_foreach_entry_prefetch - Entry iteration with prefetching
 * 
 * This is where it gets interesting: we prefetch the ENTRY (containing struct),
 * not just the node. This means the data you're about to access is already
 * in cache when you get there.
 */
#define ilist_foreach_entry_prefetch(pos, head, member) \
    for (pos = ilist_first_entry(head, typeof(*pos), member); \
         !ilist_is_head(&pos->member, head) && \
             (ilist_prefetch_read(ilist_next_entry(pos, member)), 1); \
         pos = ilist_next_entry(pos, member))

#define ilist_foreach_entry_reverse_prefetch(pos, head, member) \
    for (pos = ilist_last_entry(head, typeof(*pos), member); \
         !ilist_is_head(&pos->member, head) && \
             (ilist_prefetch_read(ilist_prev_entry(pos, member)), 1); \
         pos = ilist_prev_entry(pos, member))

#define ilist_foreach_entry_safe_prefetch(pos, tmp, head, member) \
    for (pos = ilist_first_entry(head, typeof(*pos), member), \
         tmp = ilist_next_entry(pos, member); \
         !ilist_is_head(&pos->member, head) && \
             (ilist_prefetch_read(&ilist_next_entry(tmp, member)->member), 1); \
         pos = tmp, tmp = ilist_next_entry(tmp, member))


/* ============================================================================
 * Standard Entry Iteration (Non-prefetch)
 * ============================================================================ */

#define ilist_foreach_entry(pos, head, member) \
    for (pos = ilist_first_entry(head, typeof(*pos), member); \
         !ilist_is_head(&pos->member, head); \
         pos = ilist_next_entry(pos, member))

#define ilist_foreach_entry_reverse(pos, head, member) \
    for (pos = ilist_last_entry(head, typeof(*pos), member); \
         !ilist_is_head(&pos->member, head); \
         pos = ilist_prev_entry(pos, member))

#define ilist_foreach_entry_safe(pos, tmp, head, member) \
    for (pos = ilist_first_entry(head, typeof(*pos), member), \
         tmp = ilist_next_entry(pos, member); \
         !ilist_is_head(&pos->member, head); \
         pos = tmp, tmp = ilist_next_entry(tmp, member))

#define ilist_foreach_entry_reverse_safe(pos, tmp, head, member) \
    for (pos = ilist_last_entry(head, typeof(*pos), member), \
         tmp = ilist_prev_entry(pos, member); \
         !ilist_is_head(&pos->member, head); \
         pos = tmp, tmp = ilist_prev_entry(tmp, member))


/* ============================================================================
 * Deep Prefetch Iteration (Prefetch N nodes ahead)
 * ============================================================================ */

/**
 * For very large structures or high-latency memory, prefetch multiple
 * nodes ahead. The optimal distance depends on:
 *   - Memory latency (~200-400 cycles)
 *   - Work done per iteration
 *   - Structure size
 * 
 * Rule of thumb: prefetch_distance = memory_latency / cycles_per_iteration
 */

#define ilist_foreach_entry_prefetch_n(pos, head, member, n, prefetch_array) \
    for (size_t __pf_i = 0, \
         __pf_init = ({ \
             ilist_node_t *__n = (head)->node.next; \
             for (size_t __j = 0; __j < (n) && !ilist_is_head(__n, head); __j++) { \
                 (prefetch_array)[__j] = __n; \
                 ilist_prefetch_read(ilist_entry(__n, typeof(*pos), member)); \
                 __n = __n->next; \
             } \
             0; \
         }), \
         pos = ilist_first_entry(head, typeof(*pos), member); \
         !ilist_is_head(&pos->member, head); \
         pos = ilist_next_entry(pos, member), __pf_i++)


/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * ilist_count - Count with prefetching
 */
ILIST_FORCE_INLINE size_t ilist_count(const ilist_head_t *head) {
    size_t count = 0;
    const ilist_node_t *node;
    
    for (node = head->node.next; node != &head->node; node = node->next) {
        ilist_prefetch_read(node->next);
        count++;
    }
    return count;
}

/**
 * ilist_count_max - Count with early termination
 * 
 * Useful when you only need to know "is count > N?"
 */
ILIST_FORCE_INLINE size_t ilist_count_max(const ilist_head_t *head, size_t max) {
    size_t count = 0;
    const ilist_node_t *node = head->node.next;
    
    while (count < max && node != &head->node) {
        node = node->next;
        count++;
    }
    return count;
}

ILIST_FORCE_INLINE void ilist_reverse(ilist_head_t *head) {
    if (ilist_unlikely(ilist_is_empty(head) || ilist_is_singular(head))) {
        return;
    }
    
    ilist_node_t *current = &head->node;
    ilist_node_t *tmp;
    
    do {
        tmp = current->next;
        current->next = current->prev;
        current->prev = tmp;
        current = tmp;
    } while (current != &head->node);
}

ILIST_FORCE_INLINE ilist_node_t *ilist_nth(const ilist_head_t *head, size_t n) {
    const ilist_node_t *node = head->node.next;
    
    while (n > 0 && node != &head->node) {
        ilist_prefetch_read(node->next);
        node = node->next;
        n--;
    }
    return (node != &head->node) ? (ilist_node_t *)node : NULL;
}

#define ilist_nth_entry(head, n, type, member) \
    ilist_entry_safe(ilist_nth(head, n), type, member)


/* ============================================================================
 * Sorted Insert (Optimized)
 * ============================================================================ */

#define ilist_insert_sorted(head, new_node, cmp, type, member) do { \
    ilist_node_t *__pos; \
    ilist_node_t *__head_node = &(head)->node; \
    type *__new = ilist_entry(new_node, type, member); \
    for (__pos = __head_node->next; __pos != __head_node; __pos = __pos->next) { \
        ilist_prefetch_read(__pos->next); \
        type *__cur = ilist_entry(__pos, type, member); \
        if ((cmp)(__new, __cur) < 0) break; \
    } \
    ilist_insert_before(__pos, new_node); \
} while (0)


/* ============================================================================
 * Validation (Debug)
 * ============================================================================ */

#ifdef ILIST_DEBUG

ILIST_FORCE_INLINE bool ilist_validate(const ilist_head_t *head) {
    const ilist_node_t *node = &head->node;
    const ilist_node_t *prev = head->node.prev;
    size_t count = 0;
    const size_t max_iter = 1000000;
    
    do {
        if (node->prev != prev) return false;
        if (node->next->prev != node) return false;
        prev = node;
        node = node->next;
        count++;
    } while (node != &head->node && count < max_iter);
    
    return node == &head->node;
}

#include <stdio.h>
ILIST_FORCE_INLINE void ilist_debug_print(const ilist_head_t *head, const char *name) {
    printf("List '%s' @ %p:\n", name, (void *)head);
    printf("  Head: prev=%p next=%p\n", 
           (void *)head->node.prev, (void *)head->node.next);
    
    size_t i = 0;
    const ilist_node_t *node;
    for (node = head->node.next; node != &head->node; node = node->next) {
        printf("  [%zu] @ %p: prev=%p next=%p\n", 
               i++, (void *)node, (void *)node->prev, (void *)node->next);
    }
    printf("  Total: %zu nodes\n", i);
}

#endif /* ILIST_DEBUG */


#ifdef __cplusplus
}
#endif

#endif /* INTRUSIVE_LIST_FAST_H */
