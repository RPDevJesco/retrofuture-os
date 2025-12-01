/**
 * intrusive_list.h - Fully Featured Intrusive Doubly-Linked List
 * 
 * An intrusive list embeds the node directly within the data structure,
 * eliminating separate allocations and enabling O(1) removal without search.
 * 
 * Key advantages:
 *   - No extra memory allocation for list nodes
 *   - O(1) removal when you have a pointer to the element
 *   - An element can exist in multiple lists simultaneously
 *   - Better cache locality (no pointer chasing to separate node structs)
 * 
 * Usage:
 *   struct my_data {
 *       int value;
 *       ilist_node_t list_link;    // Embed the node
 *       ilist_node_t other_list;   // Can be in multiple lists!
 *   };
 * 
 *   ILIST_HEAD(my_list);
 *   struct my_data item = { .value = 42 };
 *   ilist_init_node(&item.list_link);
 *   ilist_push_back(&my_list, &item.list_link);
 * 
 *   // Iterate and access containing structure
 *   ilist_node_t *node;
 *   ilist_foreach(node, &my_list) {
 *       struct my_data *data = ilist_entry(node, struct my_data, list_link);
 *       printf("%d\n", data->value);
 *   }
 * 
 * Author: Jesse (with Claude)
 * License: Public Domain / MIT
 */

#ifndef INTRUSIVE_LIST_H
#define INTRUSIVE_LIST_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Core Structures
 * ============================================================================ */

/**
 * ilist_node_t - Intrusive list node
 * 
 * Embed this within your data structure. The list is circular: an empty
 * list head points to itself. This simplifies insertion/removal logic
 * and eliminates NULL checks.
 */
typedef struct ilist_node {
    struct ilist_node *next;
    struct ilist_node *prev;
} ilist_node_t;

/**
 * ilist_head_t - List head (sentinel node)
 * 
 * Technically identical to ilist_node_t, but using a separate type
 * helps distinguish list heads from embedded nodes in APIs.
 */
typedef struct ilist_head {
    ilist_node_t node;
} ilist_head_t;


/* ============================================================================
 * Utility Macros
 * ============================================================================ */

/**
 * container_of - Get pointer to containing structure from member pointer
 * @ptr:    Pointer to the member
 * @type:   Type of the containing structure
 * @member: Name of the member within the structure
 * 
 * This is the magic that makes intrusive lists work. Given a pointer to the
 * embedded ilist_node_t, retrieve a pointer to the containing structure.
 */
#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

/**
 * ilist_entry - Get the struct containing this list node
 * @ptr:    Pointer to ilist_node_t
 * @type:   Type of the containing structure
 * @member: Name of the ilist_node_t member in the structure
 */
#define ilist_entry(ptr, type, member) \
    container_of(ptr, type, member)

/**
 * ilist_entry_safe - Same as ilist_entry but returns NULL if ptr is NULL
 */
#define ilist_entry_safe(ptr, type, member) \
    ((ptr) ? ilist_entry(ptr, type, member) : NULL)

/**
 * ilist_first_entry - Get first element from a list
 * @head:   Pointer to ilist_head_t
 * @type:   Type of the containing structure
 * @member: Name of the ilist_node_t member
 * 
 * Note: List must not be empty!
 */
#define ilist_first_entry(head, type, member) \
    ilist_entry((head)->node.next, type, member)

/**
 * ilist_last_entry - Get last element from a list
 * @head:   Pointer to ilist_head_t
 * @type:   Type of the containing structure  
 * @member: Name of the ilist_node_t member
 * 
 * Note: List must not be empty!
 */
#define ilist_last_entry(head, type, member) \
    ilist_entry((head)->node.prev, type, member)

/**
 * ilist_first_entry_or_null - Get first element or NULL if empty
 */
#define ilist_first_entry_or_null(head, type, member) \
    (ilist_is_empty(head) ? NULL : ilist_first_entry(head, type, member))

/**
 * ilist_last_entry_or_null - Get last element or NULL if empty
 */
#define ilist_last_entry_or_null(head, type, member) \
    (ilist_is_empty(head) ? NULL : ilist_last_entry(head, type, member))

/**
 * ilist_next_entry - Get next element in list
 * @pos:    Current element pointer (struct type, not node)
 * @member: Name of the ilist_node_t member
 */
#define ilist_next_entry(pos, member) \
    ilist_entry((pos)->member.next, typeof(*(pos)), member)

/**
 * ilist_prev_entry - Get previous element in list
 * @pos:    Current element pointer (struct type, not node)
 * @member: Name of the ilist_node_t member
 */
#define ilist_prev_entry(pos, member) \
    ilist_entry((pos)->member.prev, typeof(*(pos)), member)


/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * ILIST_HEAD_INIT - Static initializer for a list head
 * @name: Variable name of the list head
 */
#define ILIST_HEAD_INIT(name) { .node = { .next = &(name).node, .prev = &(name).node } }

/**
 * ILIST_HEAD - Declare and initialize a list head
 * @name: Variable name for the list head
 */
#define ILIST_HEAD(name) \
    ilist_head_t name = ILIST_HEAD_INIT(name)

/**
 * ILIST_NODE_INIT - Static initializer for a node (unlinked state)
 */
#define ILIST_NODE_INIT { .next = NULL, .prev = NULL }

/**
 * ilist_init_head - Initialize a list head at runtime
 * @head: Pointer to ilist_head_t
 */
static inline void ilist_init_head(ilist_head_t *head) {
    head->node.next = &head->node;
    head->node.prev = &head->node;
}

/**
 * ilist_init_node - Initialize a node (marks it as unlinked)
 * @node: Pointer to ilist_node_t
 * 
 * Sets pointers to NULL to indicate the node is not in any list.
 * This allows ilist_is_linked() to work correctly.
 */
static inline void ilist_init_node(ilist_node_t *node) {
    node->next = NULL;
    node->prev = NULL;
}

/**
 * ilist_init_node_self - Initialize node pointing to itself
 * @node: Pointer to ilist_node_t
 * 
 * Alternative initialization where node points to itself.
 * Useful when you want the node to be "its own list".
 */
static inline void ilist_init_node_self(ilist_node_t *node) {
    node->next = node;
    node->prev = node;
}


/* ============================================================================
 * State Queries
 * ============================================================================ */

/**
 * ilist_is_empty - Check if list has no elements
 * @head: Pointer to ilist_head_t
 */
static inline bool ilist_is_empty(const ilist_head_t *head) {
    return head->node.next == &head->node;
}

/**
 * ilist_is_singular - Check if list has exactly one element
 * @head: Pointer to ilist_head_t
 */
static inline bool ilist_is_singular(const ilist_head_t *head) {
    return !ilist_is_empty(head) && (head->node.next == head->node.prev);
}

/**
 * ilist_is_linked - Check if a node is currently in a list
 * @node: Pointer to ilist_node_t
 * 
 * Works only if node was initialized with ilist_init_node().
 */
static inline bool ilist_is_linked(const ilist_node_t *node) {
    return node->next != NULL;
}

/**
 * ilist_is_first - Check if node is first in list
 * @node: Pointer to ilist_node_t
 * @head: Pointer to ilist_head_t
 */
static inline bool ilist_is_first(const ilist_node_t *node, const ilist_head_t *head) {
    return node->prev == &head->node;
}

/**
 * ilist_is_last - Check if node is last in list
 * @node: Pointer to ilist_node_t
 * @head: Pointer to ilist_head_t
 */
static inline bool ilist_is_last(const ilist_node_t *node, const ilist_head_t *head) {
    return node->next == &head->node;
}

/**
 * ilist_is_head - Check if node is the head sentinel
 * @node: Pointer to ilist_node_t
 * @head: Pointer to ilist_head_t
 */
static inline bool ilist_is_head(const ilist_node_t *node, const ilist_head_t *head) {
    return node == &head->node;
}


/* ============================================================================
 * Core Operations (Internal)
 * ============================================================================ */

/**
 * __ilist_insert - Insert a node between two consecutive nodes
 * @node: New node to insert
 * @prev: Node that will precede the new node
 * @next: Node that will follow the new node
 * 
 * Internal function - use the public API instead.
 */
static inline void __ilist_insert(ilist_node_t *node, ilist_node_t *prev, ilist_node_t *next) {
    next->prev = node;
    node->next = next;
    node->prev = prev;
    prev->next = node;
}

/**
 * __ilist_remove - Remove a node by updating its neighbors
 * @prev: Previous node
 * @next: Next node
 * 
 * Internal function - use the public API instead.
 */
static inline void __ilist_remove(ilist_node_t *prev, ilist_node_t *next) {
    next->prev = prev;
    prev->next = next;
}


/* ============================================================================
 * Insertion Operations
 * ============================================================================ */

/**
 * ilist_insert_after - Insert node after a reference node
 * @ref:  Reference node (already in list)
 * @node: Node to insert
 */
static inline void ilist_insert_after(ilist_node_t *ref, ilist_node_t *node) {
    __ilist_insert(node, ref, ref->next);
}

/**
 * ilist_insert_before - Insert node before a reference node
 * @ref:  Reference node (already in list)
 * @node: Node to insert
 */
static inline void ilist_insert_before(ilist_node_t *ref, ilist_node_t *node) {
    __ilist_insert(node, ref->prev, ref);
}

/**
 * ilist_push_front - Add node to front of list (after head)
 * @head: Pointer to ilist_head_t
 * @node: Node to insert
 */
static inline void ilist_push_front(ilist_head_t *head, ilist_node_t *node) {
    __ilist_insert(node, &head->node, head->node.next);
}

/**
 * ilist_push_back - Add node to back of list (before head)
 * @head: Pointer to ilist_head_t
 * @node: Node to insert
 */
static inline void ilist_push_back(ilist_head_t *head, ilist_node_t *node) {
    __ilist_insert(node, head->node.prev, &head->node);
}


/* ============================================================================
 * Removal Operations
 * ============================================================================ */

/**
 * ilist_remove - Remove a node from its list
 * @node: Node to remove
 * 
 * Does not reinitialize the node - call ilist_init_node() if you
 * need to check ilist_is_linked() afterward or reinsert later.
 */
static inline void ilist_remove(ilist_node_t *node) {
    __ilist_remove(node->prev, node->next);
}

/**
 * ilist_remove_init - Remove node and reinitialize it
 * @node: Node to remove
 * 
 * Safely removes and marks node as unlinked.
 */
static inline void ilist_remove_init(ilist_node_t *node) {
    __ilist_remove(node->prev, node->next);
    ilist_init_node(node);
}

/**
 * ilist_pop_front - Remove and return first node
 * @head: Pointer to ilist_head_t
 * 
 * Returns NULL if list is empty. Does not reinitialize the returned node.
 */
static inline ilist_node_t *ilist_pop_front(ilist_head_t *head) {
    if (ilist_is_empty(head)) {
        return NULL;
    }
    ilist_node_t *node = head->node.next;
    ilist_remove(node);
    return node;
}

/**
 * ilist_pop_back - Remove and return last node
 * @head: Pointer to ilist_head_t
 * 
 * Returns NULL if list is empty. Does not reinitialize the returned node.
 */
static inline ilist_node_t *ilist_pop_back(ilist_head_t *head) {
    if (ilist_is_empty(head)) {
        return NULL;
    }
    ilist_node_t *node = head->node.prev;
    ilist_remove(node);
    return node;
}


/* ============================================================================
 * Movement Operations
 * ============================================================================ */

/**
 * ilist_move_to_front - Move node to front of (possibly different) list
 * @head: Target list
 * @node: Node to move (must already be in a list)
 */
static inline void ilist_move_to_front(ilist_head_t *head, ilist_node_t *node) {
    ilist_remove(node);
    ilist_push_front(head, node);
}

/**
 * ilist_move_to_back - Move node to back of (possibly different) list
 * @head: Target list
 * @node: Node to move (must already be in a list)
 */
static inline void ilist_move_to_back(ilist_head_t *head, ilist_node_t *node) {
    ilist_remove(node);
    ilist_push_back(head, node);
}

/**
 * ilist_replace - Replace one node with another
 * @old_node: Node to replace (will be unlinked)
 * @new_node: Node to insert in its place
 */
static inline void ilist_replace(ilist_node_t *old_node, ilist_node_t *new_node) {
    new_node->next = old_node->next;
    new_node->next->prev = new_node;
    new_node->prev = old_node->prev;
    new_node->prev->next = new_node;
}

/**
 * ilist_replace_init - Replace node and reinitialize old node
 * @old_node: Node to replace
 * @new_node: Replacement node
 */
static inline void ilist_replace_init(ilist_node_t *old_node, ilist_node_t *new_node) {
    ilist_replace(old_node, new_node);
    ilist_init_node(old_node);
}

/**
 * ilist_swap - Swap positions of two nodes
 * @a: First node
 * @b: Second node
 * 
 * Both nodes must be in lists (can be same or different lists).
 * Handles the adjacent node edge case.
 */
static inline void ilist_swap(ilist_node_t *a, ilist_node_t *b) {
    if (a == b) return;
    
    ilist_node_t *a_prev = a->prev;
    ilist_node_t *a_next = a->next;
    ilist_node_t *b_prev = b->prev;
    ilist_node_t *b_next = b->next;
    
    // Handle adjacent nodes
    if (a->next == b) {
        a->next = b_next;
        a->prev = b;
        b->next = a;
        b->prev = a_prev;
        a_prev->next = b;
        b_next->prev = a;
    } else if (b->next == a) {
        b->next = a_next;
        b->prev = a;
        a->next = b;
        a->prev = b_prev;
        b_prev->next = a;
        a_next->prev = b;
    } else {
        // Non-adjacent
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
 * List Splicing Operations
 * ============================================================================ */

/**
 * __ilist_splice - Join two lists (internal)
 * @list:  List to splice in
 * @prev:  Node in target list that will precede the spliced nodes
 * @next:  Node in target list that will follow the spliced nodes
 */
static inline void __ilist_splice(const ilist_head_t *list, 
                                   ilist_node_t *prev, 
                                   ilist_node_t *next) {
    ilist_node_t *first = list->node.next;
    ilist_node_t *last = list->node.prev;
    
    first->prev = prev;
    prev->next = first;
    
    last->next = next;
    next->prev = last;
}

/**
 * ilist_splice_front - Splice entire list to front of another
 * @src:  Source list (will be emptied)
 * @dst:  Destination list
 * 
 * All nodes from src are moved to the front of dst. src becomes empty.
 */
static inline void ilist_splice_front(ilist_head_t *src, ilist_head_t *dst) {
    if (!ilist_is_empty(src)) {
        __ilist_splice(src, &dst->node, dst->node.next);
        ilist_init_head(src);
    }
}

/**
 * ilist_splice_back - Splice entire list to back of another
 * @src:  Source list (will be emptied)
 * @dst:  Destination list
 * 
 * All nodes from src are moved to the back of dst. src becomes empty.
 */
static inline void ilist_splice_back(ilist_head_t *src, ilist_head_t *dst) {
    if (!ilist_is_empty(src)) {
        __ilist_splice(src, dst->node.prev, &dst->node);
        ilist_init_head(src);
    }
}

/**
 * ilist_cut_before - Cut list into two at given position
 * @head:    Original list
 * @new_head: New list to receive first portion
 * @node:    Cut point (this node will be first in original list after cut)
 * 
 * Moves all nodes before 'node' into 'new_head'. 'node' and all following
 * nodes remain in 'head'.
 */
static inline void ilist_cut_before(ilist_head_t *head, 
                                     ilist_head_t *new_head, 
                                     ilist_node_t *node) {
    if (ilist_is_empty(head) || node == head->node.next) {
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
 * Rotation Operations
 * ============================================================================ */

/**
 * ilist_rotate_left - Move first element to back
 * @head: List to rotate
 */
static inline void ilist_rotate_left(ilist_head_t *head) {
    if (!ilist_is_empty(head)) {
        ilist_move_to_back(head, head->node.next);
    }
}

/**
 * ilist_rotate_right - Move last element to front
 * @head: List to rotate
 */
static inline void ilist_rotate_right(ilist_head_t *head) {
    if (!ilist_is_empty(head)) {
        ilist_move_to_front(head, head->node.prev);
    }
}


/* ============================================================================
 * Iteration Macros (Node-Based)
 * ============================================================================ */

/**
 * ilist_foreach - Iterate over list nodes (forward)
 * @pos:  Loop variable (ilist_node_t *)
 * @head: Pointer to ilist_head_t
 * 
 * WARNING: Do not remove nodes during iteration! Use ilist_foreach_safe.
 */
#define ilist_foreach(pos, head) \
    for (pos = (head)->node.next; \
         !ilist_is_head(pos, head); \
         pos = pos->next)

/**
 * ilist_foreach_reverse - Iterate over list nodes (backward)
 * @pos:  Loop variable (ilist_node_t *)
 * @head: Pointer to ilist_head_t
 */
#define ilist_foreach_reverse(pos, head) \
    for (pos = (head)->node.prev; \
         !ilist_is_head(pos, head); \
         pos = pos->prev)

/**
 * ilist_foreach_safe - Safe iteration allowing removal
 * @pos:  Loop variable (ilist_node_t *)
 * @tmp:  Temporary variable (ilist_node_t *)
 * @head: Pointer to ilist_head_t
 * 
 * Safe to call ilist_remove(pos) during iteration.
 */
#define ilist_foreach_safe(pos, tmp, head) \
    for (pos = (head)->node.next, tmp = pos->next; \
         !ilist_is_head(pos, head); \
         pos = tmp, tmp = pos->next)

/**
 * ilist_foreach_reverse_safe - Safe reverse iteration
 * @pos:  Loop variable (ilist_node_t *)
 * @tmp:  Temporary variable (ilist_node_t *)
 * @head: Pointer to ilist_head_t
 */
#define ilist_foreach_reverse_safe(pos, tmp, head) \
    for (pos = (head)->node.prev, tmp = pos->prev; \
         !ilist_is_head(pos, head); \
         pos = tmp, tmp = pos->prev)

/**
 * ilist_foreach_from - Iterate starting from specific node
 * @pos:  Loop variable and starting node (ilist_node_t *)
 * @head: Pointer to ilist_head_t
 */
#define ilist_foreach_from(pos, head) \
    for (; !ilist_is_head(pos, head); pos = pos->next)

/**
 * ilist_foreach_from_safe - Safe iteration from specific node
 * @pos:  Loop variable and starting node (ilist_node_t *)
 * @tmp:  Temporary variable (ilist_node_t *)
 * @head: Pointer to ilist_head_t
 */
#define ilist_foreach_from_safe(pos, tmp, head) \
    for (tmp = pos->next; !ilist_is_head(pos, head); pos = tmp, tmp = pos->next)


/* ============================================================================
 * Iteration Macros (Entry-Based)
 * ============================================================================ */

/**
 * ilist_foreach_entry - Iterate over entries (forward)
 * @pos:    Loop variable (struct type *)
 * @head:   Pointer to ilist_head_t
 * @member: Name of ilist_node_t member in struct
 */
#define ilist_foreach_entry(pos, head, member) \
    for (pos = ilist_first_entry(head, typeof(*pos), member); \
         !ilist_is_head(&pos->member, head); \
         pos = ilist_next_entry(pos, member))

/**
 * ilist_foreach_entry_reverse - Iterate over entries (backward)
 * @pos:    Loop variable (struct type *)
 * @head:   Pointer to ilist_head_t
 * @member: Name of ilist_node_t member in struct
 */
#define ilist_foreach_entry_reverse(pos, head, member) \
    for (pos = ilist_last_entry(head, typeof(*pos), member); \
         !ilist_is_head(&pos->member, head); \
         pos = ilist_prev_entry(pos, member))

/**
 * ilist_foreach_entry_safe - Safe entry iteration allowing removal
 * @pos:    Loop variable (struct type *)
 * @tmp:    Temporary variable (struct type *)
 * @head:   Pointer to ilist_head_t
 * @member: Name of ilist_node_t member in struct
 */
#define ilist_foreach_entry_safe(pos, tmp, head, member) \
    for (pos = ilist_first_entry(head, typeof(*pos), member), \
         tmp = ilist_next_entry(pos, member); \
         !ilist_is_head(&pos->member, head); \
         pos = tmp, tmp = ilist_next_entry(tmp, member))

/**
 * ilist_foreach_entry_reverse_safe - Safe reverse entry iteration
 * @pos:    Loop variable (struct type *)
 * @tmp:    Temporary variable (struct type *)
 * @head:   Pointer to ilist_head_t
 * @member: Name of ilist_node_t member in struct
 */
#define ilist_foreach_entry_reverse_safe(pos, tmp, head, member) \
    for (pos = ilist_last_entry(head, typeof(*pos), member), \
         tmp = ilist_prev_entry(pos, member); \
         !ilist_is_head(&pos->member, head); \
         pos = tmp, tmp = ilist_prev_entry(tmp, member))


/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * ilist_count - Count nodes in list
 * @head: Pointer to ilist_head_t
 * 
 * O(n) - consider tracking count externally if needed frequently.
 */
static inline size_t ilist_count(const ilist_head_t *head) {
    size_t count = 0;
    const ilist_node_t *node;
    for (node = head->node.next; node != &head->node; node = node->next) {
        count++;
    }
    return count;
}

/**
 * ilist_reverse - Reverse the order of a list in-place
 * @head: List to reverse
 */
static inline void ilist_reverse(ilist_head_t *head) {
    if (ilist_is_empty(head) || ilist_is_singular(head)) {
        return;
    }
    
    ilist_node_t *current = &head->node;
    ilist_node_t *tmp;
    
    // Swap next/prev for all nodes including head
    do {
        tmp = current->next;
        current->next = current->prev;
        current->prev = tmp;
        current = tmp;
    } while (current != &head->node);
}

/**
 * ilist_nth - Get nth node (0-indexed)
 * @head: Pointer to ilist_head_t
 * @n:    Index (0 = first element)
 * 
 * Returns NULL if n >= list length.
 */
static inline ilist_node_t *ilist_nth(const ilist_head_t *head, size_t n) {
    const ilist_node_t *node = head->node.next;
    while (n > 0 && node != &head->node) {
        node = node->next;
        n--;
    }
    return (node != &head->node) ? (ilist_node_t *)node : NULL;
}

/**
 * ilist_nth_entry - Get nth entry (0-indexed)
 * @head:   Pointer to ilist_head_t
 * @n:      Index (0 = first element)
 * @type:   Type of the containing structure
 * @member: Name of ilist_node_t member in struct
 */
#define ilist_nth_entry(head, n, type, member) \
    ilist_entry_safe(ilist_nth(head, n), type, member)


/* ============================================================================
 * Debug/Validation Utilities
 * ============================================================================ */

#ifdef ILIST_DEBUG

/**
 * ilist_validate - Validate list integrity (debug builds only)
 * @head: List to validate
 * 
 * Returns true if list is consistent, false if corruption detected.
 * Checks that all forward/backward links are consistent.
 */
static inline bool ilist_validate(const ilist_head_t *head) {
    const ilist_node_t *node = &head->node;
    const ilist_node_t *prev = head->node.prev;
    size_t count = 0;
    const size_t max_iter = 1000000; // Prevent infinite loop on corruption
    
    do {
        if (node->prev != prev) return false;
        if (node->next->prev != node) return false;
        prev = node;
        node = node->next;
        count++;
    } while (node != &head->node && count < max_iter);
    
    return node == &head->node;
}

/**
 * ilist_debug_print - Print list structure (requires printf)
 * @head: List to print
 * @name: Name to display
 */
#include <stdio.h>
static inline void ilist_debug_print(const ilist_head_t *head, const char *name) {
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


/* ============================================================================
 * Sorted Insertion (Optional)
 * ============================================================================ */

/**
 * ilist_insert_sorted - Insert maintaining sorted order
 * @head:     List (must already be sorted)
 * @new_node: Node to insert
 * @cmp:      Comparison function: returns <0 if a<b, 0 if a==b, >0 if a>b
 * @type:     Structure type
 * @member:   Name of ilist_node_t member
 * 
 * Usage:
 *   int cmp_by_value(struct item *a, struct item *b) {
 *       return a->value - b->value;
 *   }
 *   ilist_insert_sorted(&list, &item.link, cmp_by_value, struct item, link);
 */
#define ilist_insert_sorted(head, new_node, cmp, type, member) do { \
    ilist_node_t *__pos; \
    ilist_node_t *__head_node = &(head)->node; \
    type *__new = ilist_entry(new_node, type, member); \
    for (__pos = __head_node->next; __pos != __head_node; __pos = __pos->next) { \
        type *__cur = ilist_entry(__pos, type, member); \
        if ((cmp)(__new, __cur) < 0) break; \
    } \
    ilist_insert_before(__pos, new_node); \
} while (0)


#ifdef __cplusplus
}
#endif

#endif /* INTRUSIVE_LIST_H */
