#include "common/common.h"
#include "shell/shell.h"

int init_entry_list(SHELL_ENTRY_LIST *list) {
    memset(list, 0, sizeof(SHELL_ENTRY_LIST));
    return 0;
}

int add_entry_list(SHELL_ENTRY_LIST *list, const SHELL_ENTRY *entry) {
    SHELL_ENTRY_LIST_ITEM *newItem = malloc(sizeof(SHELL_ENTRY_LIST_ITEM));

    if (newItem == nullptr)
        return -1;
    newItem->entry = *entry;
    newItem->next = nullptr;

    if (list->count == 0)
        list->first = list->last = newItem;
    else {
        list->last->next = newItem;
        list->last = newItem;
    }
    list->count++;
    return 0;
}

void release_entry_list(SHELL_ENTRY_LIST *list) {
    SHELL_ENTRY_LIST_ITEM *currentItem = list->first;
    SHELL_ENTRY_LIST_ITEM *nextItem;

    while (currentItem) {
        nextItem = currentItem->next;
        free(currentItem);
        currentItem = nextItem;
    }
    list->count = 0;
    list->first = nullptr;
    list->last = nullptr;
}
