#include <pthread.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>

#include "eventlist.h"
#include "operations.h"
#include "parser.h"
#include "constants.h"
#include <sys/stat.h>


#define MAX_SIZE 100000

// Definir uma estrutura para os argumentos da thread
struct ThreadArgs {
    int fdRead;   // Descritor de arquivo de leitura
    int fdWrite;  // Descritor de arquivo de escrita
    pthread_mutex_t *mutex;  // mutex para a leitura e gravação
    int id; // thread id (tid)
    int *wait_flags; // array de flags para as threads esperarem
    int num_threads; // número de threads
};

static struct EventList* event_list = NULL;
static unsigned int state_access_delay_ms = 0;

pthread_mutex_t write_file_mutex; // mutex para escrever no ficheiro
pthread_mutex_t memory_mutex; // mutex para aceder à memória

unsigned int wait_id = 0; // id da thread a esperar
unsigned int wait_time = 0; // tempo de espera

int foundBarrier = 0; // flag para barreira

/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

/// Gets the event with the given ID from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event_id The ID of the event to get.
/// @return Pointer to the event if found, NULL otherwise.
static struct Event* get_event_with_delay(unsigned int event_id) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return get_event(event_list, event_id);
}


/// Gets the seat with the given index from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event Event to get the seat from.
/// @param index Index of the seat to get.
/// @return Pointer to the seat.
static unsigned int* get_seat_with_delay(struct Event* event, size_t index) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return &event->data[index];
}

/// Gets the index of a seat.
/// @note This function assumes that the seat exists.
/// @param event Event to get the seat index from.
/// @param row Row of the seat.
/// @param col Column of the seat.
/// @return Index of the seat.
static size_t seat_index(struct Event* event, size_t row, size_t col) { return (row - 1) * event->cols + col - 1; }

int ems_init(unsigned int delay_ms) {
  if (event_list != NULL) {
    fprintf(stderr, "EMS state has already been initialized\n");
    return 1;
  }

  event_list = create_list();
  state_access_delay_ms = delay_ms;

  return event_list == NULL;
}

int ems_terminate() {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  free_list(event_list);
  event_list = NULL;
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (get_event_with_delay(event_id) != NULL) {
    fprintf(stderr, "Event already exists\n");
    return 1;
  }

  struct Event* event = malloc(sizeof(struct Event));

  if (event == NULL) {
    fprintf(stderr, "Error allocating memory for event\n");
    return 1;
  }

  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  event->reservations = 0;
  event->data = malloc(num_rows * num_cols * sizeof(unsigned int));
  event->mutexes = malloc(num_rows * num_cols * sizeof(pthread_mutex_t)); // mutex para cada lugar
  if (pthread_mutex_init(&event->event_mutex, NULL) != 0) {
    fprintf(stderr, "Error initializing mutex\n");
    return 1;
  }

  // Em caso de erro, liberta memória alocada
  if (event->data == NULL || event->mutexes == NULL) {
    fprintf(stderr, "Error allocating memory for event data\n");
    free(event->data);
    free(event->mutexes);
    free(event);
    return 1;
  }
  
  // Inicializa mutex de todos os lugares
  for (size_t i = 0; i < num_rows * num_cols; i++) {
    event->data[i] = 0;
    if (pthread_mutex_init(&event->mutexes[i],NULL) != 0) {
      fprintf(stderr, "Error initializing mutex\n");
      return 1;
    }
  }
  
  // Em caso de erro a juntar à lista de eventos, liberta memória alocada
  if (pthread_mutex_lock(&memory_mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    return 1;
  }
  if (append_to_list(event_list, event) != 0) {
    fprintf(stderr, "Error appending event to list\n");
    free(event->mutexes);
    free(event->data);
    free(event);
    if (pthread_mutex_unlock(&memory_mutex) != 0) {
      fprintf(stderr, "Error unlocking mutex\n");
      return 1;
    }
    return 1;
  }
  if (pthread_mutex_unlock(&memory_mutex) != 0) {
    fprintf(stderr, "Error unlocking mutex\n");
    return 1;
  }

  return 0;
}

int ems_free_event(unsigned int event_id) {
  struct Event* event = get_event_with_delay(event_id);

  if (event != NULL) {
    free(event->data);
    free(event);
  }

  return 0;
}

// Troca dois lugares nos vetores xs e ys
void switch_seats(size_t* xs, size_t* ys, size_t i, size_t j) {
    size_t temp = xs[i];
    xs[i] = xs[j];
    xs[j] = temp;

    temp = ys[i];
    ys[i] = ys[j];
    ys[j] = temp;
}

// Ordena lugares utilizando um algoritmo bubblesort
int sort_seats(size_t num_seats, size_t* xs, size_t* ys) {
    // Algoritmo de ordenação
    for (size_t i = 0; i < num_seats - 1; i++) {
        for (size_t j = 0; j < num_seats - i - 1; j++) {
            // Ordena por linhas
            if (xs[j] > xs[j + 1]) {
                switch_seats(xs, ys, j, j + 1);
            }
            // Se as linhas forem iguais, ordena por colunas
            else if (xs[j] == xs[j + 1] && ys[j] == ys[j + 1]) {
              return 1;
            }
            else if (xs[j] == xs[j + 1] && ys[j] > ys[j + 1]) {
                switch_seats(xs, ys, j, j + 1);
            }
        }
    }
    return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_mutex_lock(&memory_mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    return 1;
  } 
  struct Event* event = get_event_with_delay(event_id);
  if (pthread_mutex_unlock(&memory_mutex) != 0) {
    fprintf(stderr, "Error unlocking mutex\n");
    return 1;
  }

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  } 

  // Ordena lugares antes de os reservar
  if(sort_seats(num_seats, xs, ys)) {
    return 1;
  } 
  
  // Bloqueia os mutexes dos lugares a reservar
  for (size_t a = 0; a < num_seats; a++) {
    if (pthread_mutex_lock(&event->mutexes[seat_index(event, xs[a], ys[a])]) != 0) {
      fprintf(stderr, "Error locking mutex\n");
    }
  }

  // Verifica se os lugares estão disponíveis
  size_t i = 0;
  for (; i < num_seats; i++) {
    size_t row = xs[i];
    size_t col = ys[i];

    if (row <= 0 || row > event->rows || col <= 0 || col > event->cols) {
      fprintf(stderr, "Invalid seat\n");
      break;
    }
    if (*get_seat_with_delay(event, seat_index(event, row, col)) != 0) {
      fprintf(stderr, "Seat already reserved\n");
      break;
    }
  }

  // Se os lugares estiverem disponíveis, reserva-os
  if(i == num_seats) {
    if (pthread_mutex_lock(&event->event_mutex) != 0) {
      fprintf(stderr, "Error locking mutex\n");
      return 1;
    }
    unsigned int reservation_id = ++event->reservations;
    if (pthread_mutex_unlock(&event->event_mutex) != 0) {
      fprintf(stderr, "Error unlocking mutex\n");
      return 1;
    }
    for ( i = 0; i < num_seats; i++) {
      size_t row = xs[i];
      size_t col = ys[i];
      
      *get_seat_with_delay(event, seat_index(event, row, col)) = reservation_id;
      
    }
  }

  // Se a reserva falhar, desbloqueia os mutexes dos lugares
  else if (i < num_seats) {
    for (size_t k = 0; k < num_seats; k++) {
      if (pthread_mutex_unlock(&event->mutexes[seat_index(event, xs[k], ys[k])]) != 0) {
        fprintf(stderr, "Error unlocking mutex\n");
        return 1;
      }
    }
    return 1;
  }
  
  for (size_t k = 0; k < num_seats; k++) {
    if (pthread_mutex_unlock(&event->mutexes[seat_index(event, xs[k], ys[k])]) != 0) {
      fprintf(stderr, "Error unlocking mutex\n");
      return 1;
    }
  }
  return 0;
}

int ems_show(int fd, unsigned int event_id) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_mutex_lock(&memory_mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    return 1;
  } 
  struct Event* event = get_event_with_delay(event_id);
  if (pthread_mutex_unlock(&memory_mutex) != 0) {
    fprintf(stderr, "Error unlocking mutex\n");
    return 1;
  }

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  // Escreve os lugares num buffer
  char buffer[MAX_SIZE];
  int bytes_written = 0;
  int offset = 0;

  // Bloqueia os mutexes dos lugares a mostrar
  for (size_t i = 1; i <= event->rows; i++) {
    for (size_t j = 1; j <= event->cols; j++) {
      if (pthread_mutex_lock(&event->mutexes[seat_index(event,i,j)]) != 0) {
        fprintf(stderr, "Error locking mutex\n");
        return 1;
      }
      unsigned int* seat = get_seat_with_delay(event, seat_index(event, i, j));
      bytes_written = ((int) snprintf(buffer + offset, (unsigned long) (MAX_SIZE - offset), "%u", *seat));

      if (bytes_written < 0 || bytes_written >= MAX_SIZE - offset) {
        fprintf(stderr, "Failed to write in buffer\n");
        for (size_t a = 1; a <= i; a++) {
          for(size_t b = 1; b <= j; b++) {
            if (pthread_mutex_unlock(&event->mutexes[seat_index(event,a,b)]) != 0) {
              fprintf(stderr, "Error unlocking mutex\n");
              return 1;
            }
          }
        }
        return 1;
      }
      offset += bytes_written;

      if (j < event->cols) {
        offset = writeStringToBuffer(buffer, offset, " ");
      }
     
    }
    offset = writeStringToBuffer(buffer, offset, "\n");
  }

  // Desbloqueia os mutexes dos lugares
  for (size_t i = 1; i <= event->rows; i++) {
    for (size_t j = 1; j <= event->cols; j++) {
      if (pthread_mutex_unlock(&event->mutexes[seat_index(event,i,j)]) != 0) {
        fprintf(stderr, "Error unlocking mutex\n");
        return 1;
      }
    }
  }

  // Escreve do buffer para o ficheiro utilizando o seu file descriptor
  if (pthread_mutex_lock(&write_file_mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    return 1;
  }
  size_t len = (size_t) strlen(buffer);
  int done = 0;
  while (len > 0) {
    bytes_written = (int) write(fd, buffer + done, len);

    if (bytes_written < 0){
        fprintf(stderr, "Failed to write in file: %s\n", strerror(errno));
        if (pthread_mutex_unlock(&write_file_mutex) != 0) {
          fprintf(stderr, "Error unlocking mutex\n");
          return 1;
        }
        return -1;
    }

    // Pode não ter conseguido escrever tudo, len torna-se o que falta
    len -= (size_t) bytes_written;
    done += bytes_written;
  }
  if (pthread_mutex_unlock(&write_file_mutex) != 0) {
    fprintf(stderr, "Error unlocking mutex\n");
    return 1;
  }

  return 0;
}

int ems_list_events(int fd) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  char buffer[MAX_SIZE];
  int bytes_written = 0;
  int offset = 0;


  // Escreve a informação num buffer
  if (pthread_mutex_lock(&memory_mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    return 1;
  } 
  if (event_list->head == NULL) {
    offset = writeStringToBuffer(buffer, offset, "No events\n");
  }

  struct ListNode* current = event_list->head;
  while (current != NULL) {
    bytes_written = ((int) snprintf(buffer + offset, (unsigned long) (MAX_SIZE - offset), "Event: %u\n", (current->event)->id));

    if (bytes_written < 0 || bytes_written >= MAX_SIZE - offset) {
      fprintf(stderr, "Failed to write in buffer\n");
      if (pthread_mutex_unlock(&memory_mutex) != 0) {
        fprintf(stderr, "Error unlocking mutex\n");
        return 1;
      } 
      return -1;
    }
    offset += bytes_written;
    current = current->next;
  }
  if (pthread_mutex_unlock(&memory_mutex) != 0) {
    fprintf(stderr, "Error unlocking mutex\n");
    return 1;
  }

  // Escreve do buffer para o ficheiro utilizando o seu file descriptor
  if (pthread_mutex_lock(&write_file_mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    return 1;
  }
  size_t len = (size_t) strlen(buffer);
  int done = 0;
  while (len > 0) {
    bytes_written = (int) write(fd, buffer + done, len);

    if (bytes_written < 0){
        fprintf(stderr, "Failed to write in file: %s\n", strerror(errno));
        if (pthread_mutex_unlock(&write_file_mutex) != 0) {
          fprintf(stderr, "Error unlocking mutex\n");
          return 1;
        }
        return -1;
    }

    // Pode não ter conseguido escrever tudo, len torna-se o que falta
    len -= (size_t) bytes_written;
    done += bytes_written;
  }
  if (pthread_mutex_unlock(&write_file_mutex) != 0) {
    fprintf(stderr, "Error unlocking mutex\n");
    return 1;
  }

  return 0;
}

void ems_wait(unsigned int delay_ms) {
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}

int writeStringToBuffer(char* buffer, int offset, const char* inputString) {
    int bytes_written = ((int) snprintf(buffer + offset, (unsigned long) (MAX_SIZE - offset), "%s", inputString));

    if (bytes_written < 0 || bytes_written >= MAX_SIZE - offset) {
        fprintf(stderr, "Falha ao escrever na memória do buffer\n");
        return -1;
    }

    return offset + bytes_written; // Retorna o novo offset
}

// Função da thread
void* execute_commands(void *args) {
  struct ThreadArgs *threadArgs = (struct ThreadArgs *)args;
  int fdRead = threadArgs->fdRead;
  int fdWrite = threadArgs->fdWrite;
  pthread_mutex_t *mutex_t = threadArgs->mutex;
  int thread_id = threadArgs->id;

  unsigned int event_id;
  size_t num_rows, num_columns, num_coords;
  size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

  int flag = 1;
  while (flag == 1) {
    // Bloqueia o mutex para a leitura e escrita
    if (pthread_mutex_lock(mutex_t) != 0) {
      fprintf(stderr, "Error locking mutex\n");
      return (void*) 1;
    }
    
    // Verifica se a flag de barreira está ativa
    if(foundBarrier == 1) {
      if (pthread_mutex_unlock(mutex_t) != 0) {
        fprintf(stderr, "Error unlocking mutex\n");
        return (void*) 1;
      }
      return (void*)1;
    }
    // Espera se a flag de espera estiver ativa
    if(threadArgs->wait_flags[thread_id]) {
      printf("Waiting...\n");
      ems_wait(wait_time);
      threadArgs->wait_flags[thread_id] = 0;
    }

    // Lê o comando
    switch (get_next(fdRead)) {
        case CMD_CREATE:
          if (parse_create(fdRead, &event_id, &num_rows, &num_columns) != 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            if (pthread_mutex_unlock(mutex_t) != 0) {
              fprintf(stderr, "Error unlocking mutex\n");
              return (void*) 1;
            }
            continue;
          }

          if (ems_create(event_id, num_rows, num_columns)) {
            fprintf(stderr, "Failed to create event\n");
          }
          if (pthread_mutex_unlock(mutex_t) != 0) {
            fprintf(stderr, "Error unlocking mutex\n");
            return (void*) 1;
          }

          break;

        case CMD_RESERVE:
          num_coords = parse_reserve(fdRead, MAX_RESERVATION_SIZE, &event_id, xs, ys);

          if (pthread_mutex_unlock(mutex_t) != 0) {
            fprintf(stderr, "Error unlocking mutex\n");
            return (void*) 1;
          }

          if (num_coords == 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (ems_reserve(event_id, num_coords, xs, ys)) {
            fprintf(stderr, "Failed to reserve seats\n");
          }
          
          break;

        case CMD_SHOW:
          if (parse_show(fdRead, &event_id) != 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            if (pthread_mutex_lock(&memory_mutex) != 0) {
              fprintf(stderr, "Error locking mutex\n");
              return (void*) 1;
            } 
            continue;
          }

          if (ems_show(fdWrite, event_id)) {
            fprintf(stderr, "Failed to show event\n");
          }
          if (pthread_mutex_unlock(mutex_t) != 0) {
            fprintf(stderr, "Error unlocking mutex\n");
            return (void*) 1;
          }
          
          break;

        case CMD_LIST_EVENTS:
          if (pthread_mutex_unlock(mutex_t) != 0) {
            fprintf(stderr, "Error unlocking mutex\n");
            return (void*) 1;
          }

          if (ems_list_events(fdWrite)) {
            fprintf(stderr, "Failed to list events\n");
          }
          
          break;

        case CMD_WAIT:
          if (parse_wait(fdRead, &wait_time, &wait_id) == -1) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            if (pthread_mutex_unlock(mutex_t) != 0) {
              fprintf(stderr, "Error unlocking mutex\n");
              return (void*) 1;
            }
            continue;
          }

          if(wait_id == 0) {
            for(int i = 1 ; i < threadArgs->num_threads ; i++ ) {
              threadArgs->wait_flags[i] = 1;
            }
          }
          else {
            threadArgs->wait_flags[wait_id] = 1;
          }

          if (pthread_mutex_unlock(mutex_t) != 0) {
            fprintf(stderr, "Error unlocking mutex\n");
            return (void*) 1;
          }

          break;

        case CMD_INVALID:
          if (pthread_mutex_unlock(mutex_t) != 0) {
            fprintf(stderr, "Error unlocking mutex\n");
            return (void*) 1;
          }
          fprintf(stderr, "Invalid command. See HELP for usage\n");

          break;
        
        case CMD_HELP:
          if (pthread_mutex_unlock(mutex_t) != 0) {
            fprintf(stderr, "Error unlocking mutex\n");
            return (void*) 1;
          }
          printf(
          "Available commands:\n"
          "  CREATE <event_id> <num_rows> <num_columns>\n"
          "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
          "  SHOW <event_id>\n"
          "  LIST\n"
          "  WAIT <delay_ms> [thread_id]\n"  // thread_id is not implemented
          "  BARRIER\n"                      // Not implemented
          "  HELP\n");

          break;

        case CMD_BARRIER:
          foundBarrier = 1;

          if (pthread_mutex_unlock(mutex_t) != 0) {
            fprintf(stderr, "Error unlocking mutex\n");
            return (void*) 1;
          }
          
          return (void*)1;

        case CMD_EMPTY:
          if (pthread_mutex_unlock(mutex_t) != 0) {
            fprintf(stderr, "Error unlocking mutex\n");
            return (void*) 1;
          }

          break;

        case EOC:
          if (pthread_mutex_unlock(mutex_t) != 0) {
            fprintf(stderr, "Error unlocking mutex\n");
            return (void*) 1;
          }
          flag = 0;
          break;
    }
  }
  return (void*)0;
}

int ems_execute_child(struct dirent *dp, char *dirpath, int MAX_THREADS) {
  int fdRead = 0;
  int fdWrite = 0;

  int flag = 1;

  int wait_flags[MAX_THREADS + 1];

  for(int i = 0; i <= MAX_THREADS; i++) {
    wait_flags[i] = 0;
  }

  // Constrói o caminho completo dos ficheiros de entrada e saída
  char filepathInput[strlen(dirpath) + strlen("/") + strlen(dp->d_name) + 1];
  char filepathOutput[strlen(dirpath) + strlen("/") + strlen(dp->d_name)];

  // Ficheiro de Input
  strcpy(filepathInput, dirpath);
  strcat(filepathInput, "/");
  strcat(filepathInput, dp->d_name);

  // Manipulação de strings para criação do nome do ficheiro de output
  size_t size = strlen(dp->d_name) - 5;
  char filename[size + 4 + 1];  // +4 para ".out", +1 para o caractere nulo
  strncpy(filename, dp->d_name, size);
  filename[size] = '\0';  // Adiciona o caractere nulo manualmente
  strcat(filename, ".out");

  // Ficheiro de Output
  strcpy(filepathOutput, dirpath);
  strcat(filepathOutput, "/");
  strcat(filepathOutput, filename);

  // Abre o ficheiro e cria ficheiro com extensão ".out"
  fdRead = open(filepathInput, O_RDONLY);
  if (fdRead < 0) {
    fprintf(stderr,"Failed to open .jobs file\n");
    return 1;
  }

  fdWrite = open(filepathOutput, O_CREAT | O_TRUNC | O_WRONLY , S_IRUSR | S_IWUSR);
  if (fdWrite < 0) {
    fprintf(stderr,"Failed to create output file\n");
    return 1;
  }

  // Criar e configurar mutex para secção crítica
  pthread_mutex_t mutex;

  // Criar e configurar threads
  pthread_t threads[MAX_THREADS];
  struct ThreadArgs threadArgs[MAX_THREADS];

  // Inicializa mutexes
  if (pthread_mutex_init(&mutex, NULL) != 0) {
    fprintf(stderr, "Error initializing mutex\n");
    return 1;
  }
  if (pthread_mutex_init(&write_file_mutex, NULL) != 0) {
    fprintf(stderr, "Error initializing mutex\n");
    return 1;
  }
  if (pthread_mutex_init(&memory_mutex, NULL) != 0) {
    fprintf(stderr, "Error initializing mutex\n");
    return 1;
  }
  
  while (flag) {
    foundBarrier = 0;

    for (int i = 0; i < MAX_THREADS; ++i) {
      threadArgs[i].fdRead = fdRead;
      threadArgs[i].fdWrite = fdWrite;
      threadArgs[i].mutex = &mutex;
      threadArgs[i].wait_flags = wait_flags;
      threadArgs[i].id = i + 1;
      threadArgs[i].num_threads = MAX_THREADS;

      // Criar thread
      if (pthread_create(&threads[i], 0, execute_commands, (void *)&threadArgs[i]) != 0) {
        fprintf(stderr, "Error creating thread\n");
        return 1;
      }
    }

    // Aguardar a conclusão de todas as threads
    flag = 0;
    for (int i = 0; i < MAX_THREADS; ++i) {
      void *retorno;
      if (pthread_join(threads[i], &retorno) != 0) {
        fprintf(stderr, "Error joining thread\n");
        return 1;
      }
      
      if (retorno == (void*) 1) {
        flag = 1;
      }
    }
  }

  // Destrói mutexes
  if (pthread_mutex_destroy(&write_file_mutex) != 0) {
    fprintf(stderr, "Error destroying mutex\n");
    return 1;
  }
  if (pthread_mutex_destroy(&mutex) != 0) {
    fprintf(stderr, "Error destroying mutex\n");
    return 1;
  }
  if (pthread_mutex_destroy(&memory_mutex) != 0) {
    fprintf(stderr, "Error destroying mutex\n");
    return 1;
  }

  ems_terminate();

  // Fecha os ficheiros
  if (close(fdRead) == -1) {
    fprintf(stderr,"Failed to close file\n");
    return 1;
  }

  if(close(fdWrite) == -1) {
    fprintf(stderr,"Failed to close file\n");
    return 1;
  }
  return 0;
}