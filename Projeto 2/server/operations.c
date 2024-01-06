#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#include "common/io.h"
#include "eventlist.h"
#include "operations.h"
#include "buffer_prod_cons.h"
#include "common/constants.h"

static struct EventList* event_list = NULL;
static unsigned int state_access_delay_us = 0;

int end_flag = 1;

/// Gets the event with the given ID from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event_id The ID of the event to get.
/// @param from First node to be searched.
/// @param to Last node to be searched.
/// @return Pointer to the event if found, NULL otherwise.
static struct Event* get_event_with_delay(unsigned int event_id, struct ListNode* from, struct ListNode* to) {
  struct timespec delay = {0, state_access_delay_us * 1000};
  nanosleep(&delay, NULL);  // Should not be removed

  return get_event(event_list, event_id, from, to);
}

/// Gets the index of a seat.
/// @note This function assumes that the seat exists.
/// @param event Event to get the seat index from.
/// @param row Row of the seat.
/// @param col Column of the seat.
/// @return Index of the seat.
static size_t seat_index(struct Event* event, size_t row, size_t col) { return (row - 1) * event->cols + col - 1; }

int ems_init(unsigned int delay_us) {
  if (event_list != NULL) {
    fprintf(stderr, "EMS state has already been initialized\n");
    return 1;
  }

  event_list = create_list();
  state_access_delay_us = delay_us;

  return event_list == NULL;
}

int ems_terminate() {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_wrlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  free_list(event_list);
  pthread_rwlock_unlock(&event_list->rwl);
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {

  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_wrlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  if (get_event_with_delay(event_id, event_list->head, event_list->tail) != NULL) {
    fprintf(stderr, "Event already exists\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  struct Event* event = malloc(sizeof(struct Event));

  if (event == NULL) {
    fprintf(stderr, "Error allocating memory for event\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  event->reservations = 0;
  if (pthread_mutex_init(&event->mutex, NULL) != 0) {
    pthread_rwlock_unlock(&event_list->rwl);
    free(event);
    return 1;
  }
  event->data = calloc(num_rows * num_cols, sizeof(unsigned int));

  if (event->data == NULL) {
    fprintf(stderr, "Error allocating memory for event data\n");
    pthread_rwlock_unlock(&event_list->rwl);
    free(event);
    return 1;
  }

  if (append_to_list(event_list, event) != 0) {
    fprintf(stderr, "Error appending event to list\n");
    pthread_rwlock_unlock(&event_list->rwl);
    free(event->data);
    free(event);
    return 1;
  }

  pthread_rwlock_unlock(&event_list->rwl);
  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id, event_list->head, event_list->tail);

  pthread_rwlock_unlock(&event_list->rwl);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  if (pthread_mutex_lock(&event->mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    return 1;
  }

  for (size_t i = 0; i < num_seats; i++) {
    if (xs[i] <= 0 || xs[i] > event->rows || ys[i] <= 0 || ys[i] > event->cols) {
      fprintf(stderr, "Seat out of bounds\n");
      pthread_mutex_unlock(&event->mutex);
      return 1;
    }
  }

  for (size_t i = 0; i < event->rows * event->cols; i++) {
    for (size_t j = 0; j < num_seats; j++) {
      if (seat_index(event, xs[j], ys[j]) != i) {
        continue;
      }

      if (event->data[i] != 0) {
        fprintf(stderr, "Seat already reserved\n");
        pthread_mutex_unlock(&event->mutex);
        return 1;
      }

      break;
    }
  }

  unsigned int reservation_id = ++event->reservations;

  for (size_t i = 0; i < num_seats; i++) {
    event->data[seat_index(event, xs[i], ys[i])] = reservation_id;
  }

  pthread_mutex_unlock(&event->mutex);
  return 0;
}

int ems_show(char **message, unsigned int event_id) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id, event_list->head, event_list->tail);

  pthread_rwlock_unlock(&event_list->rwl);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  if (pthread_mutex_lock(&event->mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    return 1;
  }

  char info[2 * sizeof(size_t) + (event->rows * event->cols) * sizeof(size_t)];
  memcpy(&info, &event->rows, sizeof(size_t));
  memcpy(&info[sizeof(size_t)], &event->cols, sizeof(size_t));

  // Copy data to info
  size_t data_index = 2 * sizeof(size_t);
  for (size_t i = 1; i <= event->rows; i++) {
    for (size_t j = 1; j <= event->cols; j++) {
      // Copy string representation of data to info
      memcpy(&info[data_index], &event->data[seat_index(event, i, j)], sizeof(size_t));
      data_index += sizeof(size_t);
    }
  }
  pthread_mutex_unlock(&event->mutex);

  *message = malloc(2 * sizeof(size_t) + (event->rows * event->cols) * sizeof(size_t));
  if (message == NULL) {
    fprintf(stderr, "Error allocating memory\n");
    return 1;
  }
  memcpy(*message, info, 2 * sizeof(size_t) + (event->rows * event->cols) * sizeof(size_t));
  return 0;
}

int ems_list_events(char **message) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  struct ListNode* to = event_list->tail;
  struct ListNode* current = event_list->head;

  if (current == NULL) {
    
    size_t num = 0;
    *message = malloc(sizeof(size_t));
    if (message == NULL) {
      fprintf(stderr, "Error allocating memory\n");
      return 1;
    }
    memcpy(*message, &num, sizeof(size_t));

    pthread_rwlock_unlock(&event_list->rwl);
    return 0;
  }
  
  size_t data_index = 1;
  *message = malloc(sizeof(size_t) + sizeof(unsigned int));
  if (message == NULL) {
      fprintf(stderr, "Error allocating memory\n");
      return 1;
  }
  while (1) {
    // Calcular o novo tamanho
    size_t new_size = sizeof(size_t) + ((data_index) * sizeof(unsigned int));
    
    // Realocar o bloco de memória
    void *temp = realloc(*message, new_size);
    if (temp == NULL) {
      fprintf(stderr, "Error allocating memory\n");
      free(*message);
      pthread_rwlock_unlock(&event_list->rwl);
      return 1;
    }
    *message = temp;
    
    // Copiar o novo ID
    memcpy(*message + sizeof(size_t) + ((data_index - 1) * sizeof(unsigned int)), &((current->event)->id), sizeof(unsigned int));

    if (current == to) {
        break;
    }
    data_index++;
    current = current->next;
  }
  memcpy(*message, &data_index, sizeof(size_t));

  pthread_rwlock_unlock(&event_list->rwl);
  return 0;
}

int ems_setup(int session_id, struct Session *session) {
  //TODO: Write new client to the producer-consumer buffer
  int resp_fd = open(session->resp_pipe_path, O_WRONLY);
  if (resp_fd == -1) {
    fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
    ems_terminate();
    return 1;
  }

  char session_id_str[sizeof(int)]; // Garantir que o número cabe no buffer
  memcpy(session_id_str, &session_id, sizeof(int));

  // Return session_id to client
  if (print_str(resp_fd, session_id_str)) {
    fprintf(stderr, "Error writing in response pipe\n");
    return 1;
  }
  if (close(resp_fd) == -1) {
    fprintf(stderr, "Error closing response pipe\n");
    return 1;
  }
  return 0;
}

void parse_create(char buffer[MAX_SIZE_PATHS], unsigned int *event_id, size_t *num_rows, size_t *num_cols) {
  memcpy(event_id, &buffer[1], sizeof(unsigned int));
  memcpy(num_rows, &buffer[1 + sizeof(unsigned int)], sizeof(size_t));
  memcpy(num_cols, &buffer[1 + sizeof(unsigned int) + sizeof(size_t)], sizeof(size_t));
}

void* execute_commands(void *args) {
  // Verifica sinal
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR1);
  if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0) {
      perror("Blocking SIGUSR1 in thread failed");
      exit(EXIT_FAILURE);
  }

  struct ThreadArgs *threadArgs = (struct ThreadArgs *)args;
  pthread_mutex_t *mutex_t = threadArgs->mutex;
  int thread_id = threadArgs->id;
  struct Session *session = malloc(sizeof(struct Session));
  pthread_cond_t *cond_var = threadArgs->cond_var;
  pthread_mutex_t *mutex_cond = threadArgs->mutex_cond;
  pthread_rwlock_t *buffer_lock = threadArgs->buffer_lock;
  
  while (1) {
    int flag = 1;
    unsigned int event_id;
    ssize_t bytesRead;
    size_t num_seats = 0;
    size_t num_rows;
    size_t num_cols;
    char *list = NULL;
    char *message_list = NULL;
    int response_val_list;
    int resp_fd_list;
    int OP_CODE = 0;
    char buffer[82];

    //bloqueia se o buffer estiver vazio
    if (pthread_mutex_lock(mutex_cond) != 0) {
      fprintf(stderr, "Error locking condition mutex\n");
      return (void *)1;
    }
    if (pthread_rwlock_rdlock(buffer_lock) != 0) {
      fprintf(stderr, "Error locking buffer lock\n");
      return (void *)1;
    }
    while(head_null()) {
      pthread_rwlock_unlock(buffer_lock);
      if (pthread_cond_wait(cond_var, mutex_cond) != 0) {
        fprintf(stderr, "Error waiting for condition\n");
        pthread_mutex_unlock(mutex_cond);
        return (void *)1;
      }
      if (pthread_rwlock_rdlock(buffer_lock) != 0) {
        fprintf(stderr, "Error locking read and write lock\n");
        return (void *)1;
      }
    }
    pthread_rwlock_unlock(buffer_lock);
    pthread_mutex_unlock(mutex_cond);
    
    //extrair pedido de incio de sessão ao buffer
    if( pthread_rwlock_wrlock(buffer_lock) != 0) {
      fprintf(stderr, "Error locking read and write lock\n");
      return (void *)1;
    }
    removeFirstNode(session);
    pthread_rwlock_unlock(buffer_lock);
   
    if (pthread_cond_signal(cond_var) != 0) {
      fprintf(stderr, "[ERR]: pthread_cond_signal failed\n");
      return (void *)1;
    }

    // Faz ems setup
    ems_setup(thread_id, session);

    while(flag) {
      // Open request pipe for reading
      int req_fd = open(session->req_pipe_path, O_RDWR);
      if (req_fd == -1) {
        fprintf(stderr, "[ERR]: open request pipe failed: %s\n", strerror(errno));
        ems_terminate();
        return (void*)1;
      }
      //TODO: Read from pipe
      bytesRead = read(req_fd, buffer, sizeof(buffer));
      if (bytesRead == -1) {
        fprintf(stderr, "[ERR]: read from request pipe failed: %s\n", strerror(errno));
        close(req_fd);
        ems_terminate();
        return (void*)1;
      }
      if (close(req_fd) == -1) {
        fprintf(stderr, "[ERR]: close request pipe failed: %s\n", strerror(errno));
        ems_terminate();
        return (void*)1;
      }
      OP_CODE = buffer[0] - '0';
    
      switch(OP_CODE) {
        
        case EMS_QUIT:
          if (pthread_mutex_lock(mutex_t) != 0) {
            fprintf(stderr, "Error locking mutex\n");
            return (void *)1;
          }
          flag = 0;
          pthread_mutex_unlock(mutex_t);
          break;
        
        case EMS_CREATE:
          if (pthread_mutex_lock(mutex_t) != 0) {
            fprintf(stderr, "Error locking mutex\n");
            return (void *)1;
          }
          // Obtém dados enviados pela request pipe
          parse_create(buffer, &event_id, &num_rows, &num_cols);
          
          // Chama ems_create() com os dados fornecidos
          int response_val = ems_create(event_id, num_rows, num_cols);

          // Retorna valor ao cliente pela response pipe
          int resp_fd = open(session->resp_pipe_path, O_WRONLY);
          if (resp_fd == -1) {
            fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
            pthread_mutex_unlock(mutex_t);
            return (void*)1;
          }
          char response[sizeof(int)];
          memcpy(&response, &response_val, sizeof(int));

          if (print_str_size(resp_fd, response, sizeof(int))) {
            fprintf(stderr, "Error writing in response pipe\n");
            return (void *)1;
          }
          if (close(resp_fd) == -1) {
            fprintf(stderr, "Error closing response pipe\n");
            return (void *)1;
          }
          pthread_mutex_unlock(mutex_t);

          break;

        case EMS_RESERVE:
          
          if (pthread_mutex_lock(mutex_t) != 0) {
            fprintf(stderr, "Error locking mutex\n");
            return (void *)1;
          }
         
          // Obtém dados enviados pela request pipe
          memcpy(&event_id, &buffer[1], sizeof(unsigned int));
          memcpy(&num_seats, &buffer[1 + sizeof(unsigned int)], sizeof(size_t));
          size_t *xs = malloc(num_seats * sizeof(size_t));
          if (xs == NULL) {
            fprintf(stderr, "Error allocating memory\n");
            return (void *)1;
          }
          size_t *ys = malloc(num_seats * sizeof(size_t));
          if (ys == NULL) {
            fprintf(stderr, "Error allocating memory\n");
            return (void *)1;
          }
          memcpy(xs, &buffer[1 + sizeof(unsigned int) + sizeof(size_t)], num_seats * sizeof(size_t));
          memcpy(ys, &buffer[1 + sizeof(unsigned int) + sizeof(size_t) + num_seats * sizeof(size_t)], num_seats * sizeof(size_t));

          // Chama ems_reserve() com os dados fornecidos
          response_val = ems_reserve(event_id, num_seats, xs, ys);

          // Retorna valor ao cliente pela response pipe
          resp_fd = open(session->resp_pipe_path, O_WRONLY);
          if (resp_fd == -1) {
            fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
            pthread_mutex_unlock(mutex_t);
             return (void*)1;
          }
          memcpy(&response, &response_val, sizeof(int));
          if (print_str_size(resp_fd, response, sizeof(int))) {
            fprintf(stderr, "Error writing in response pipe\n");
            return (void *)1;
          }
          if (close(resp_fd) == -1) {
            fprintf(stderr, "Error closing response pipe\n");
            return (void *)1;
          }
          pthread_mutex_unlock(mutex_t);
          break;
        
        case EMS_SHOW:
          if (pthread_mutex_lock(mutex_t) != 0) {
            fprintf(stderr, "Error locking mutex\n");
            return (void *)1;
          }
          // Obtém dados enviados pela request pipe
          memcpy(&event_id, &buffer[1], sizeof(unsigned int));
          char *ptr = NULL;
          int response_val_show;
          int resp_fd_show;

          // Chama ems_show() e preenche buffer com resultado
          response_val_show = ems_show(&ptr, event_id);
          if(response_val_show) {
            char erro[sizeof(int)];
            memcpy(erro, &response_val_show, sizeof(int));

            resp_fd_show = open(session->resp_pipe_path, O_WRONLY);
            if (resp_fd_show == -1) {
              fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
              pthread_mutex_unlock(mutex_t);
               return (void*)1;
            }
            if (print_str_size(resp_fd_show, erro, sizeof(int))) {
              fprintf(stderr, "Error writing in response pipe\n");
              return (void*)1;
            }
            if (close(resp_fd_show) == -1) {
              fprintf(stderr, "Error closing response pipe\n");
              return (void*)1;
            }
            pthread_mutex_unlock(mutex_t);
            break;
          }

          size_t rows, cols;
          memcpy(&rows, ptr, sizeof(size_t));
          memcpy(&cols, ptr + sizeof(size_t), sizeof(size_t));

          char *message = malloc(sizeof(int) + 2 * sizeof(size_t) + (rows * cols) * sizeof(size_t));
          if (message == NULL) {
            fprintf(stderr, "Error allocating memory\n");
            return (void*)1;
          }
          memcpy(message, &response_val_show, sizeof(int));
          memcpy(message + sizeof(int), ptr, 2 * sizeof(size_t) + (rows * cols) * sizeof(size_t));
          
          // Retorna valor ao cliente pela response pipe
          resp_fd_show = open(session->resp_pipe_path, O_WRONLY);
          if (resp_fd_show == -1) {
            fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
            pthread_mutex_unlock(mutex_t);
             return (void*)1;
          }
          if (print_str_size(resp_fd_show, message, sizeof(int) + 2 * sizeof(size_t) + (rows * cols) * sizeof(size_t))) {
            fprintf(stderr, "Error writing in response pipe\n");
            return (void*)1;
          }
          if (close(resp_fd_show) == -1) {
            fprintf(stderr, "Error closing response pipe\n");
            return (void*)1;
          }
          free(message);
          pthread_mutex_unlock(mutex_t);

          break;
        
        case EMS_LIST_EVENTS:
          if (pthread_mutex_lock(mutex_t) != 0) {
            fprintf(stderr, "Error locking mutex\n");
            return (void*)1;
          }
         
          response_val_list = ems_list_events(&list);
          if(response_val_list) {
            char erro[sizeof(int)];
            memcpy(erro, &response_val_list, sizeof(int));

            resp_fd_list = open(session->resp_pipe_path, O_WRONLY);
            if (resp_fd_list == -1) {
              fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
              pthread_mutex_unlock(mutex_t);
               return (void*)1;
            }
            if (print_str_size(resp_fd_list, erro, sizeof(int))) {
              fprintf(stderr, "Error writing in response pipe\n");
              return (void*)1;
            }
            
            if(close(resp_fd_list) == -1) {
              fprintf(stderr, "Error closing response pipe\n");
              return (void*)1;
            }
            pthread_mutex_unlock(mutex_t);
            break;
          }

          size_t num_events;
          memcpy(&num_events, list, sizeof(size_t));
          size_t size = 0;

          // Cria mensagem a ser passada ao cliente pela pipe
          size = sizeof(int) + sizeof(size_t) + num_events * sizeof(unsigned int);
          if (num_events != 0) {
            message_list = malloc(sizeof(int) + sizeof(size_t) + num_events * sizeof(unsigned int));
            if (message_list == NULL) {
              fprintf(stderr, "Error allocating memory\n");
              return (void*)1;
            }
            memcpy(message_list + sizeof(int), list, sizeof(size_t) + num_events * sizeof(unsigned int));
            memcpy(message_list, &response_val_list, sizeof(int));
          }
          
          // Retorna valor ao cliente pela response pipe
          resp_fd_list = open(session->resp_pipe_path, O_WRONLY);
          if (resp_fd_list == -1) {
            fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
            pthread_mutex_unlock(mutex_t);
             return (void*)1;
          }
          if (print_str_size(resp_fd_list, message_list, size)) {
            fprintf(stderr, "Error writing in response pipe\n");
            return (void*)1;
          }
          if(close(resp_fd_list) == -1) {
            fprintf(stderr, "Error closing response pipe\n");
            return (void*)1;
          }
          free(list);
          free(message_list);
          pthread_mutex_unlock(mutex_t);
          break;
      }
    }
  }
   return (void*)0;
}



int signal_show() {  
  struct ListNode* event_node = event_list->head;

  if(event_node == NULL) {
    printf("No Events\n");
    return 0;
  }

  struct Event* event;

  while (event_node != NULL) {
    event = event_node->event;
    printf("Event id: %d\n", event->id);
    for (size_t i = 1; i <= event->rows; i++) {
      for (size_t j = 1; j <= event->cols; j++) {
        printf("%d",event->data[seat_index(event, i, j)]);
        if(j < event->cols) {
          printf(" ");
        }
      }
      printf("\n");
    }
    event_node = event_node->next;
  }
  return 0;
}