#include <stdlib.h>
#include <string.h>
#include "dslink/col/listener.h"


void add_listener(Dispatcher *dispatcher, int (*callback)(void*, void*), void *data) {
    Listener *listener = malloc(sizeof(Listener));
    listener->callback = callback;
    listener->data = data;
    insert_list_node(&dispatcher->list, listener);
}

void dispatch_message(Dispatcher *dispatcher, void *message) {
    dslink_list_foreach(&dispatcher->list) {
        Listener *listener = (Listener *)node;
        listener->callback(listener->data, message);
    }
}

void dispatch_and_remove_all(Dispatcher *dispatcher, void *message) {
    dslink_list_foreach(&dispatcher->list) {
        Listener *listener = (Listener *)node;
        listener->callback(listener->data, message);
        // clear it from the list
        listener->list = NULL;
    }
    dispatcher->list.head.next = &dispatcher->list.head;
    dispatcher->list.head.prev = &dispatcher->list.head;
}