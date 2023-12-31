#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "common/io.h"
#include "eventlist.h"
#include "operations.h"

static struct EventList* event_list = NULL;
static unsigned int state_access_delay_us = 0;

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
  printf("entrou operations\n");
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

int ems_setup(char buffer[82], struct Session *session) {
  session->session_id = 1;

  // Parsing da mensagem de pedido
  memcpy(session->req_pipe_path, &buffer[1], 40);
  memcpy(session->resp_pipe_path, &buffer[41], 40);

  //TODO: Write new client to the producer-consumer buffer
  int resp_fd = open(session->resp_pipe_path, O_WRONLY);
  if (resp_fd == -1) {
    fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
    ems_terminate();
    return 1;
  }

  char session_id_str[20]; // Garantir que o número cabe no buffer
  sprintf(session_id_str, "%d", session->session_id);

  // Return session_id to client
  print_str(resp_fd, session_id_str);
  close(resp_fd);
  return 0;
}