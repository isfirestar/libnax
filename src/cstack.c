#include "cstack.h"

#include <stdlib.h>

struct cstack_node *cstack_push(struct cstack_node *stack, struct cstack_node *node)
{
    if (!node) {
        return stack;
    }

    if (!stack) {
        node->next = NULL;
        node->bottom = node;
        return node;
    }

    node->next = stack;
    node->bottom = stack->bottom;
    return node;
}

struct cstack_node *cstack_pop(struct cstack_node *stack, struct cstack_node **removed)
{
    struct cstack_node *top;

    if (!stack) {
        return NULL;
    }

    /* there are only one node in stack */
    if (stack == stack->bottom) {
        if (removed) {
            *removed = stack;
        }
        stack->bottom = NULL;
        return NULL;
    }

    top = stack->next;
    if (removed) {
        *removed = stack;
    }
    stack->bottom = NULL;
    return top;
}

