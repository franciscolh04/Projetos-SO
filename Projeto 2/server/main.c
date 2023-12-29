#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"

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
  // Lê paths de pipes de request e response e conecta-se a eles
  char buffer[82]; // MAX SIZE OF 2 PATHS (verificar depois)
  ssize_t bytesRead = read(server_fd, buffer, sizeof(buffer));
  if (bytesRead == -1) {
    fprintf(stderr, "[ERR]: read from server pipe failed: %s\n", strerror(errno));
    close(server_fd);
    ems_terminate();
    return 1;
  }
  int OP_CODE = buffer[0] - '0';
  struct Session *session = malloc(sizeof(struct Session));

  while (1) {
    if (initialized_server) {
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
    }

    switch(OP_CODE) {
      case EMS_SETUP:
        printf("entrou ems_setup\n");
        ems_setup(buffer, session);
        initialized_server = 1;
        break;
      
      case EMS_QUIT:
        printf("entrou quit\n");
        break;
      
      case EMS_CREATE:
        printf("entrou create\n");
        // Obtém dados enviados pela request pipe
        unsigned int event_id;
        size_t num_rows;
        size_t num_cols;
        memcpy(&event_id, &buffer[1], sizeof(unsigned int));
        memcpy(&num_rows, &buffer[1 + sizeof(unsigned int)], sizeof(size_t));
        memcpy(&num_cols, &buffer[1 + sizeof(unsigned int) + sizeof(size_t)], sizeof(size_t));

        // JÁ RECEBE OS DADOS BEM, FALTA CHAMAR O EMS_CREATE DO SERVER E RETORNAR VALOR PARA O CLIENTE

        // Chama ems_create() com os dados fornecidos

        // Retorna valor ao cliente pela response pipe
        break;

      case EMS_RESERVE:
        printf("entrou reserve\n");
        // Obtém dados enviados pela request pipe
        size_t num_seats;
        size_t *xs = malloc(num_seats * sizeof(size_t));
        size_t *ys = malloc(num_seats * sizeof(size_t));
        memcpy(&event_id, &buffer[1], sizeof(unsigned int));
        memcpy(&num_seats, &buffer[1 + sizeof(unsigned int)], sizeof(size_t));
        memcpy(xs, &buffer[1 + sizeof(unsigned int) + sizeof(size_t)], num_seats * sizeof(size_t));
        memcpy(ys, &buffer[1 + sizeof(unsigned int) + sizeof(size_t) + num_seats * sizeof(size_t)], num_seats * sizeof(size_t));

        printf("Event id: %d\n", event_id);
        printf("Num seats: %zu\n", num_seats);
        for (size_t i = 0; i < num_seats; i++) {
          printf("(xs[i], ys[i]) = (%zu, %zu)\n", xs[i], ys[i]);
        }

        // JÁ RECEBE OS DADOS BEM, FALTA CHAMAR O EMS_RESERVE DO SERVER E RETORNAR VALOR PARA O CLIENTE

        // Chama ems_reserve() com os dados fornecidos

        // Retorna valor ao cliente pela response pipe

        break;
      
      case EMS_SHOW:
        printf("entrou show\n");
        break;
      
      case EMS_LIST_EVENTS:
        printf("entrou list events\n");
        break;
      
      case EOC:
        ems_terminate();
        return 1;
    }
  }

  //TODO: Close Server
  if (close(server_fd) == -1) {
    fprintf(stderr, "[ERR]: close server pipe failed: %s\n", strerror(errno));
    ems_terminate();
    return 1;
  }

  ems_terminate();
}