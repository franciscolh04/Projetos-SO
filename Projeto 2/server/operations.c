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
#include "buffer_prod_cons.h"

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
  printf("entrou create server\n");
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
  printf("entrou reserve server\n");
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
      char buffer[sizeof(size_t)];
      snprintf(buffer, sizeof(size_t), "%u", event->data[seat_index(event, i, j)]);
      printf("%s ", buffer);

      // Copy string representation of data to info
      memcpy(&info[data_index], &event->data[seat_index(event, i, j)], sizeof(size_t));
      data_index += sizeof(size_t);
    }
    printf("\n");
  }

  pthread_mutex_unlock(&event->mutex);

  *message = malloc(2 * sizeof(size_t) + + (event->rows * event->cols) * sizeof(size_t));
  memcpy(*message, info, 2 * sizeof(size_t) + + (event->rows * event->cols) * sizeof(size_t));
  return 0;
}

int ems_list_events(char **message) {
  printf("entrou list operations\n");

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
    printf("current NULL\n");
    size_t num = 0;
    *message = malloc(sizeof(size_t));
    memcpy(*message, &num, sizeof(size_t));

    pthread_rwlock_unlock(&event_list->rwl);
    return 0;
  }
  
  size_t data_index = 1;
  *message = malloc(sizeof(size_t) + sizeof(unsigned int));
  while (1) {
    // Calcular o novo tamanho
    size_t new_size = sizeof(size_t) + ((data_index) * sizeof(unsigned int));
    
    // Realocar o bloco de memória
    void *temp = realloc(*message, new_size);
    if (temp == NULL) {
        // Trate o erro de realocação
        perror("Erro na realocação de memória");
        free(*message);  // Libere a memória original
        pthread_rwlock_unlock(&event_list->rwl);
        return 1;  // Retorne um código de erro
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
  print_str(resp_fd, session_id_str);
  close(resp_fd);
  return 0;
}


void* execute_commands(void *args) {
  struct ThreadArgs *threadArgs = (struct ThreadArgs *)args;
  pthread_mutex_t *mutex_t = threadArgs->mutex;
  int thread_id = threadArgs->id;
  struct Session *session = malloc(sizeof(struct Session));
  struct Session **head = threadArgs->head;
  pthread_cond_t *cond_var = threadArgs->cond_var;
  pthread_mutex_t *mutex_cond = threadArgs->mutex_cond;
  
  
  while (1) {
    int flag = 1;
    unsigned int event_id;
    ssize_t bytesRead;
    size_t num_seats = 0;
    size_t num_rows;
    size_t num_cols;
    char *list = NULL;
    char* message_list = NULL;
    int response_val_list;
    int resp_fd_list;
    int OP_CODE = 0;
    char buffer[82];

    //Variáveis de condição
    printf("cliente não inicializado\n");
    // Lê paths de pipes de request e response e conecta-se a eles
    pthread_mutex_lock(mutex_cond);
    printf("passou mutex com id: %d\n", thread_id);
    while(head_null()) {
      printf("entrou no while\n");
      pthread_cond_wait(cond_var, mutex_cond);
      printf("passou cond wait\n");
      if(head_null()) {
        printf("head continua a null\n");
      }
    }
    printf("passou variavel de condição\n");
    pthread_mutex_unlock(mutex_cond);
    
    pthread_mutex_lock(mutex_t);
    removeFirstNode(session);
    /*
    strcpy(session->req_pipe_path,(*head)->req_pipe_path);
    strcpy(session->resp_pipe_path,(*head)->resp_pipe_path);
    //removeFirstNode(&head);
    if ((*head)->next == NULL) {
      strcpy(session->req_pipe_path,(*head)->req_pipe_path);
      strcpy(session->resp_pipe_path,(*head)->resp_pipe_path);
      (*head) = NULL;
    }
    else {
      printf("entrou free de temp\n");
      strcpy(session->req_pipe_path,((*head)->next)->req_pipe_path);
      strcpy(session->resp_pipe_path,((*head)->next)->resp_pipe_path);
      struct Session *temp = (*head);  // Guarda a referência para o primeiro nó
      (*head) = temp->next;        // Atualiza a cabeça para apontar para o próximo nó
      //free(temp);
    }
    */


    // Libera a memória alocada para o nó removido
    printf("foi buscar a session\n");
    pthread_mutex_unlock(mutex_t);

    printf("Response pipe: %s\n", session->resp_pipe_path);
    printf("Request pipe: %s\n", session->req_pipe_path);
    ems_setup(thread_id, session);
    printf("passou set up\n");
    

    OP_CODE = 0;
    printf("OP CODE: %d\n", OP_CODE);
    while(flag) {

        printf("cliente inicializado\n");
        // Open request pipe for reading
        int req_fd = open(session->req_pipe_path, O_RDWR);
        if (req_fd == -1) {
          fprintf(stderr, "[ERR]: open request pipe failed: %s\n", strerror(errno));
          ems_terminate();

          return 1;
        }
        //TODO: Read from pipe
        bytesRead = read(req_fd, buffer, sizeof(buffer));
        if (bytesRead == -1) {
          fprintf(stderr, "[ERR]: read from request pipe failed: %s\n", strerror(errno));
          close(req_fd);
          ems_terminate();
          return 1;
        }
        if (close(req_fd) == -1) {
          fprintf(stderr, "[ERR]: close request pipe failed: %s\n", strerror(errno));
          ems_terminate();
          return 1;
        }
        OP_CODE = buffer[0] - '0';
    
      switch(OP_CODE) {
        
        case EMS_QUIT:
          printf("Entrou no quit\n");
          pthread_mutex_lock(mutex_t);
          flag = 0;
          pthread_mutex_unlock(mutex_t);
          break;
        
        case EMS_CREATE:
          printf("CREAT\n");
          pthread_mutex_lock(mutex_t);
          //printf("entrou create\n");
          // Obtém dados enviados pela request pipe
          //unsigned int event_id;
          memcpy(&event_id, &buffer[1], sizeof(unsigned int));
          memcpy(&num_rows, &buffer[1 + sizeof(unsigned int)], sizeof(size_t));
          memcpy(&num_cols, &buffer[1 + sizeof(unsigned int) + sizeof(size_t)], sizeof(size_t));

          // Chama ems_create() com os dados fornecidos
          int response_val = ems_create(event_id, num_rows, num_cols);

          // Retorna valor ao cliente pela response pipe
          int resp_fd = open(session->resp_pipe_path, O_WRONLY);
          if (resp_fd == -1) {
            fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
            pthread_mutex_unlock(mutex_t);
            return 1;
          }
          char response[sizeof(int)];
          memcpy(&response, &response_val, sizeof(int));
          print_str_size(resp_fd, response, sizeof(int));
          close(resp_fd);
          pthread_mutex_unlock(mutex_t);

          break;

        case EMS_RESERVE:
          printf("RESERVE\n");
          pthread_mutex_lock(mutex_t);
          //printf("entrou reserve\n");
          // Obtém dados enviados pela request pipe
          memcpy(&event_id, &buffer[1], sizeof(unsigned int));
          memcpy(&num_seats, &buffer[1 + sizeof(unsigned int)], sizeof(size_t));
          size_t *xs = malloc(num_seats * sizeof(size_t));
          size_t *ys = malloc(num_seats * sizeof(size_t));
          memcpy(xs, &buffer[1 + sizeof(unsigned int) + sizeof(size_t)], num_seats * sizeof(size_t));
          memcpy(ys, &buffer[1 + sizeof(unsigned int) + sizeof(size_t) + num_seats * sizeof(size_t)], num_seats * sizeof(size_t));

          // Chama ems_reserve() com os dados fornecidos
          response_val = ems_reserve(event_id, num_seats, xs, ys);

          // Retorna valor ao cliente pela response pipe
          resp_fd = open(session->resp_pipe_path, O_WRONLY);
          if (resp_fd == -1) {
            fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
            pthread_mutex_unlock(mutex_t);
            return 1;
          }
          memcpy(&response, &response_val, sizeof(int));
          print_str_size(resp_fd, response, sizeof(int));
          close(resp_fd);
          pthread_mutex_unlock(mutex_t);

          break;
        
        case EMS_SHOW:
          printf("SHOW\n");
          pthread_mutex_lock(mutex_t);
          // Obtém dados enviados pela request pipe
          memcpy(&event_id, &buffer[1], sizeof(unsigned int));
          char *ptr = NULL;
          int response_val_show;
          int resp_fd_show;

          // Chama ems_show() e preenche buffer com resultado
          if(response_val_show = ems_show(&ptr, event_id)) {
            char erro[sizeof(int)];
            memcpy(erro, &response_val_show, sizeof(int));

            resp_fd_show = open(session->resp_pipe_path, O_WRONLY);
            if (resp_fd_show == -1) {
              fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
              pthread_mutex_unlock(mutex_t);
              return 1;
            }
            print_str_size(resp_fd_show, erro, sizeof(int));
            close(resp_fd_show);
            pthread_mutex_unlock(mutex_t);
            break;
          }

          size_t rows, cols;
          memcpy(&rows, ptr, sizeof(size_t));
          memcpy(&cols, ptr + sizeof(size_t), sizeof(size_t));

          char *message = malloc(sizeof(int) + 2 * sizeof(size_t) + (rows * cols) * sizeof(size_t));
          memcpy(message, &response_val_show, sizeof(int));
          memcpy(message + sizeof(int), ptr, 2 * sizeof(size_t) + (rows * cols) * sizeof(size_t));
          
          // Retorna valor ao cliente pela response pipe
          resp_fd_show = open(session->resp_pipe_path, O_WRONLY);
          if (resp_fd_show == -1) {
            fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
            pthread_mutex_unlock(mutex_t);
            return 1;
          }
          print_str_size(resp_fd_show, message, sizeof(int) + 2 * sizeof(size_t) + (rows * cols) * sizeof(size_t));
          close(resp_fd_show);
          free(message);
          pthread_mutex_unlock(mutex_t);

          break;
        
        case EMS_LIST_EVENTS:
          printf("LIST\n");
          pthread_mutex_lock(mutex_t);
          //printf("entrou list events\n");
          // Chama ems_list_events() e preenche buffer com resultado
          //*message = NULL;

          if(response_val_list = ems_list_events(&list)) {
            char erro[sizeof(int)];
            memcpy(erro, &response_val_list, sizeof(int));

            resp_fd_list = open(session->resp_pipe_path, O_WRONLY);
            if (resp_fd_list == -1) {
              fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
              pthread_mutex_unlock(mutex_t);
              return 1;
            }
            print_str_size(resp_fd_list, erro, sizeof(int));
            close(resp_fd_list);
            pthread_mutex_unlock(mutex_t);
            break;
          }

          size_t num_events;
          memcpy(&num_events, list, sizeof(size_t));
          int size = 0;

          // Cria mensagem a ser passada ao cliente pela pipe
          size = sizeof(int) + sizeof(size_t) + num_events * sizeof(unsigned int);
          if (num_events != 0) {
            message_list = malloc(sizeof(int) + sizeof(size_t) + num_events * sizeof(unsigned int));
            memcpy(message_list + sizeof(int), list, sizeof(size_t) + num_events * sizeof(unsigned int));
            memcpy(message_list, &response_val_list, sizeof(int));
            //memcpy(message + sizeof(int), &num_events, sizeof(size_t));
          }
          
          // Retorna valor ao cliente pela response pipe
          resp_fd_list = open(session->resp_pipe_path, O_WRONLY);
          if (resp_fd_list == -1) {
            fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
            pthread_mutex_unlock(mutex_t);
            return 1;
          }
          print_str_size(resp_fd_list, message_list, size);
          close(resp_fd_list);
          free(list);
          free(message_list);
          pthread_mutex_unlock(mutex_t);
          break;
        
        case EOC:
          printf("EOC\n");
          pthread_mutex_lock(mutex_t);
          //printf("entrou EOC\n");
          ems_terminate();
          pthread_mutex_unlock(mutex_t);
          break;
      }
    }
  }
  return 0;
}
