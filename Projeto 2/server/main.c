#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"
#include "buffer_prod_cons.h"

int initialized_server = 0;

int main(int argc, char* argv[]) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: %s\n <pipe_path> [delay]\n", argv[0]);
    return 1;
  }

  char* endptr;
  unsigned int state_access_delay_us = STATE_ACCESS_DELAY_US;
  if (argc == 3) {
    unsigned long int delay = strtoul(argv[2], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_us = (unsigned int)delay;
  }

  if (ems_init(state_access_delay_us)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  //TODO: Intialize server, create worker threads
  if (mkfifo(argv[1], 0640) != 0) {
    fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
    return 1;
  }

  // Open server pipe for reading and writing
  int server_fd = open(argv[1], O_RDWR);
  if (server_fd == -1) {
    fprintf(stderr, "[ERR]: open server pipe failed: %s\n", strerror(errno));
    ems_terminate();
    return 1;
  }
  int OP_CODE = 0;
  struct Session *session = malloc(sizeof(struct Session));
  char buffer[82]; // MAX SIZE OF 2 PATHS (verificar depois)
  ssize_t bytesRead;

  pthread_t threads[MAX_SESSION_COUNT];
  struct ThreadArgs threadArgs[MAX_SESSION_COUNT];
  pthread_mutex_t mutex;
  struct Session *head = NULL;
  struct Session *tail = NULL;
  pthread_cond_t cond_var; 
  pthread_mutex_t mutex_cond;
  pthread_cond_init(&cond_var,NULL);
  pthread_mutex_init(&mutex, NULL);
  pthread_mutex_init(&mutex_cond, NULL);

  for (int i = 0; i < MAX_SESSION_COUNT; ++i) {
    threadArgs[i].mutex = &mutex;
    threadArgs[i].id = i + 1;
    //threadArgs[i].head = &head;
    threadArgs[i].cond_var = &cond_var;
    threadArgs[i].mutex_cond = &mutex_cond;

    // Criar thread
    pthread_create(&threads[i], 0, execute_commands, (void *)&threadArgs[i]);
    printf("criou thread %d\n", i + 1);
  }

  while(1) {
    bytesRead = read(server_fd, buffer, sizeof(buffer));
    printf("Leu o terminal\n");
    if (bytesRead == -1) {
      fprintf(stderr, "[ERR]: read from server pipe failed: %s\n", strerror(errno));
      close(server_fd);
      ems_terminate();
      return 1;
    }
    if(bytesRead > 0) {
      addNode(buffer);
      /*
      struct Session *new_session;
      new_session = malloc(sizeof(struct Session));
      // obtem tail da lista ligada (buffer produtor-consumidor)
      // malloc novo nó e cópia de informações para o nó
      memcpy(new_session->req_pipe_path, &buffer[1], 40);
      memcpy(new_session->resp_pipe_path, &buffer[41], 40);
      new_session->next = NULL;
      if(head == NULL) {
        printf("criou head da lista\n");
        head = new_session;
        tail = new_session;
        printf("adicionou cliente a buffer\n");
      }
      else {
        printf("criou novo elemento da lista\n");
        tail->next = new_session;
        tail = new_session;
      }
      */
      pthread_cond_signal(&cond_var);
      printf("Sinalizou \n");

      // adicionar nó ao final da lista ligada
    }
    printf("Acabou de ler o terminal \n");
  }
  

  for (int i = 0; i < MAX_SESSION_COUNT; ++i) {
    void *retorno;
    if(pthread_join(threads[i], &retorno) == ((void*) 1)) {
      return 1;
    }
  }
  pthread_mutex_destroy(&mutex);
  pthread_cond_destroy(&cond_var);

  //TODO: Close Server
  if (close(server_fd) == -1) {
    fprintf(stderr, "[ERR]: close server pipe failed: %s\n", strerror(errno));
    ems_terminate();
    return 1;
  }

  ems_terminate();
}