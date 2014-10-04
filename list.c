#include <stdio.h>
#include <stdlib.h>
#include "proj1.h"
#include "list.h"

/* Functions for handling linked lists */

/* 
 * Function to add a node to the start of the list
 */
int add_to_list(struct list_node **head, void *node)
{
    struct list_node *cur;
    cur = (struct list_node *) malloc(sizeof(struct list_node));

    bzero(cur, sizeof(struct list_node));

    cur->container = node;
    if (*head == NULL) {
        *head = cur;
        return 0;
    }
    cur->next = *head;
    /* make this as the head */
    *head = cur;

    return 0;
}

/* 
 * Function to add a node to the end of the list
 */
int add_to_list_tail(struct list_node **head, void *node)
{
    struct list_node *cur, *tmp;
    cur = (struct list_node *) malloc(sizeof(struct list_node));

    bzero(cur, sizeof(struct list_node));

    cur->container = node;

    if (*head == NULL) {
        *head = cur;
        return 0;
    }

    /* Traverse to the end of the list */
    for (tmp = *head; tmp->next != NULL; tmp = tmp->next);  /* Intended semicolon */

    tmp->next = cur;

    return 0;
}


/* 
 * Function to delete a specific node from the list
 */
int delete_from_list(struct list_node **head, void *node)
{
    struct list_node *cur, *prev = NULL;
    for (cur = *head; cur != NULL; cur = cur->next) {
        if (cur->container == node) {
            /* We have found the node, let's delete it */
            if (prev != NULL) {
                prev-> next = cur->next;
            } else {
                /* first node, make the next as head */
                *head = cur->next;
            }
            free(cur->container);
            free(cur);
            return 0;
        }
        prev = cur;
    }
    return -1;
}

