#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>

//void addNode(char *buffer,struct Session **head, struct Session **tail);

void addNode(char *buffer);

void removeFirstNode(struct Session *head);

int head_null();

#endif  // BUFFER_H
