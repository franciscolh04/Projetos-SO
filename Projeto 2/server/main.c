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
#include <signal.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"
#include "buffer_prod_cons.h"

int initialized_server = 0;
int signal_flag = 0;

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

  // Remove pipe if it already exists
  if (unlink(argv[1]) != 0 && errno != ENOENT) {
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", argv[1], strerror(errno));
    return 1;
  }

  //TODO: Intialize server, create worker threads
  if (mkfifo(argv[1], 0640) != 0) {
    fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
    return 1;
  }

  if (signal(SIGUSR1, sigusr1_signal_handler) == SIG_ERR) {
    perror("Signal handler failed\n");
    return 1;
  }

  // Open server pipe for reading and writing
  int server_fd = open(argv[1], O_RDWR);
  if (server_fd == -1) {
    fprintf(stderr, "[ERR]: open server pipe failed: %s\n", strerror(errno));
    ems_terminate();
    return 1;
  }

  char buffer[MAX_SIZE_PATHS]; // MAX SIZE OF 2 PATHS
  ssize_t bytesRead;

  pthread_t threads[MAX_SESSION_COUNT];
  struct ThreadArgs threadArgs[MAX_SESSION_COUNT];
  pthread_mutex_t mutex;
  pthread_cond_t cond_var; 
  pthread_mutex_t mutex_cond;
  pthread_rwlock_t buffer_lock;

  if (pthread_cond_init(&cond_var,NULL) != 0) {
    fprintf(stderr, "Error inicializing condition variable\n");
    return 1;
  }
  if (pthread_mutex_init(&mutex, NULL) != 0) {
    fprintf(stderr, "Error inicializing mutex\n");
    return 1;
  }
  if(pthread_mutex_init(&mutex_cond, NULL) != 0) {
    fprintf(stderr, "Error inicializing mutex condition\n");
    return 1;
  }
  if(pthread_rwlock_init(&buffer_lock, NULL) != 0) {
    fprintf(stderr, "Error inicializing read and write lock\n");
    return 1;
  }

  for (int i = 0; i < MAX_SESSION_COUNT; ++i) {
    threadArgs[i].mutex = &mutex;
    threadArgs[i].id = i + 1;
    threadArgs[i].cond_var = &cond_var;
    threadArgs[i].mutex_cond = &mutex_cond;
    threadArgs[i].buffer_lock = &buffer_lock;

    // Cria thread
    if (pthread_create(&threads[i], 0, execute_commands, (void *)&threadArgs[i]) != 0) {
      fprintf(stderr, "Error creating thread\n");
      return 1;
    }
  }

  while(1) {
    if (signal_flag) {
      // Chama função que mostra o estado de cada evento
      signal_show();
      signal_flag = 0;
      if (signal(SIGUSR1, sigusr1_signal_handler) == SIG_ERR) {
        perror("Signal handler failed\n");
        exit(EXIT_FAILURE);
      }
    }

    //bloquear o servidor se já não houver espaço na lista de espera (buffer)
    if (pthread_mutex_lock(&mutex_cond) != 0) {
      fprintf(stderr, "Error locking condition mutex\n");
      return 1;
    }
    while(list_length() >= MAX_WAIT_LIST) {
      if (pthread_cond_wait(&cond_var, &mutex_cond) != 0) {
        fprintf(stderr, "Error waiting for condition\n");
        pthread_mutex_unlock(&mutex_cond);
        return 1;
      }
    }
    if (pthread_mutex_unlock(&mutex_cond) != 0) {
      fprintf(stderr, "Error unlocking condition mutex\n");
      return 1;
    }

    //ler da pipe do servidor 
    bytesRead = read(server_fd, buffer, sizeof(buffer));
 
    if (bytesRead == -1) {
      // Trata erro EINTR
      if(errno != EINTR) {
        fprintf(stderr, "[ERR]: read from server pipe failed: %s\n", strerror(errno));
        if (close(server_fd) == -1) {
          fprintf(stderr, "[ERR]: close server pipe failed: %s\n", strerror(errno));
          return 1;
        }
        ems_terminate();
        return 1;
      }
      continue;
    }
    
    //Sinalizar as threads que a lista já não está vazia
    if(bytesRead > 0) {
      if (pthread_rwlock_wrlock(&buffer_lock) != 0) {
        fprintf(stderr, "Error locking buffer read and write lock\n");
        return 1;
      }
      if (addNode(buffer)) {
        fprintf(stderr, "Failed to add node\n");
        return 1;
      }
      if (pthread_rwlock_unlock(&buffer_lock) != 0) {
        fprintf(stderr, "Error unlocking buffer read and write lock\n");
        return 1;
      }
      if (pthread_cond_signal(&cond_var) != 0) {
        fprintf(stderr, "[ERR]: pthread_cond_signal failed\n");
        return 1;
      }
    }
  }

  for (int i = 0; i < MAX_SESSION_COUNT; ++i) {
    void *retorno;
    if(pthread_join(threads[i], &retorno) == 1) {
      return 1;
    }
  }
  
  if (pthread_mutex_destroy(&mutex) != 0 ) {
    fprintf(stderr, "Error destroying mutex\n");
    return 1;
  }
  if( pthread_cond_destroy(&cond_var) != 0) {
    fprintf(stderr, "Error destroying condition variable\n");
    return 1;
  }

  if (pthread_rwlock_destroy(&buffer_lock) != 0) {
    fprintf(stderr, "Error destroying read and write lock\n");
    return 1;
  }

  //TODO: Close Server
  if (close(server_fd) == -1) {
    fprintf(stderr, "[ERR]: close server pipe failed: %s\n", strerror(errno));
    ems_terminate();
    return 1;
  }

  ems_terminate();
}

void sigusr1_signal_handler() {
  signal_flag = 1;
}