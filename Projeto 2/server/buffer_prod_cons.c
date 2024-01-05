#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include "common/io.h"
#include "eventlist.h"
#include "operations.h"

struct Session *head;
struct Session *tail;



void addNode(char *buffer) {
    struct Session *new_session = malloc(sizeof(struct Session));
    memcpy(new_session->req_pipe_path, &buffer[1], 40);
    memcpy(new_session->resp_pipe_path, &buffer[41], 40);
    new_session->next = NULL;
    if((head) == NULL) {
        printf("criou head da lista\n");
        (head) = new_session;
        tail = new_session;
        printf("adicionou cliente a buffer\n");
    }
    else {
        printf("criou novo elemento da lista\n");
        (tail)->next = new_session;
        (tail) = new_session;
    }
}

void removeFirstNode(struct Session *session) {
    strcpy(session->req_pipe_path,head->req_pipe_path);
    strcpy(session->resp_pipe_path,head->resp_pipe_path);
    if (head->next == NULL) {
        free(head);
        head = NULL;
        tail = NULL;
        //free(head);
    }
    else {
      printf("entrou free de temp\n");
      struct Session *temp = head;  // Guarda a referência para o primeiro nó
      head = temp->next;        // Atualiza a cabeça para apontar para o próximo nó
      free(temp);
    }
}

int head_null() {
    if (head == NULL) {
        return 1;
    }
    return 0;
}