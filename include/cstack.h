#pragma once

struct cstack_node
{
    struct cstack_node *next;
    struct cstack_node *bottom;
};
typedef struct cstack_node cstack_node_t, *cstack_node_pt;

/* push a node onto the stack, return the new top
*/
struct cstack_node *cstack_push(struct cstack_node *stack, struct cstack_node *node);

/* pop a node from the stack, return the new top
* removed is set to the removed node, if this field not needed, pass NULL
*/
struct cstack_node *cstack_pop(struct cstack_node *stack, struct cstack_node **removed);

