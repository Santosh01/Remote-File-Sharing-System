#ifndef __PROJ1_LIST_H__
#define __PROJ1_LIST_H__

struct list_node {
    void *container;
    struct list_node *next;
};

int add_to_list(struct list_node **head, void *node);
int add_to_list_tail(struct list_node **head, void *node);
int delete_from_list(struct list_node **head, void *node);

#endif

