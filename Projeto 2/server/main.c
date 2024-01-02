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
  int OP_CODE = 0;
  struct Session *session = malloc(sizeof(struct Session));
  char buffer[82]; // MAX SIZE OF 2 PATHS (verificar depois)
  ssize_t bytesRead;

  while (1) {
    if (!initialized_server) {
      printf("server não inicializado\n");
      // Lê paths de pipes de request e response e conecta-se a eles
      bytesRead = read(server_fd, buffer, sizeof(buffer));
      if (bytesRead == -1) {
        fprintf(stderr, "[ERR]: read from server pipe failed: %s\n", strerror(errno));
        close(server_fd);
        ems_terminate();
        return 1;
      }
      OP_CODE = buffer[0] - '0';
      printf("OP CODE: %d\n", OP_CODE);
    }
    else {
      printf("server inicializado\n");
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
        ems_setup(buffer, session);
        initialized_server = 1;
        break;
      
      case EMS_QUIT:
        initialized_server = 0;
        if (unlink(session->req_pipe_path) != 0 && errno != ENOENT) {
          fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", session->req_pipe_path, strerror(errno));
          return 1;
        };
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

        // Chama ems_create() com os dados fornecidos
        int response_val = ems_create(event_id, num_rows, num_cols);

        // Retorna valor ao cliente pela response pipe
        int resp_fd = open(session->resp_pipe_path, O_WRONLY);
        if (resp_fd == -1) {
          fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
          return 1;
        }
        char response[sizeof(int)];
        memcpy(&response, &response_val, sizeof(int));
        print_str_size(resp_fd, response, sizeof(int));
        close(resp_fd);

        break;

      case EMS_RESERVE:
        printf("entrou reserve\n");
        // Obtém dados enviados pela request pipe
        size_t num_seats = 0;
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
          return 1;
        }
        memcpy(&response, &response_val, sizeof(int));
        print_str_size(resp_fd, response, sizeof(int));
        close(resp_fd);

        break;
      
      case EMS_SHOW:
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
            return 1;
          }
          print_str_size(resp_fd_show, erro, sizeof(int));
          close(resp_fd_show);
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
          return 1;
        }
        print_str_size(resp_fd_show, message, sizeof(int) + 2 * sizeof(size_t) + (rows * cols) * sizeof(size_t));
        close(resp_fd_show);
        free(message);

        break;
      
      case EMS_LIST_EVENTS:
        printf("entrou list events\n");
        // Chama ems_list_events() e preenche buffer com resultado
        char *list = NULL;
        char* message_list = NULL;
        //*message = NULL;
        int response_val_list;
        int resp_fd_list;

        if(response_val_list = ems_list_events(&list)) {
          char erro[sizeof(int)];
          memcpy(erro, &response_val_list, sizeof(int));

          resp_fd_list = open(session->resp_pipe_path, O_WRONLY);
          if (resp_fd_list == -1) {
            fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
            return 1;
          }
          print_str_size(resp_fd_list, erro, sizeof(int));
          close(resp_fd_list);
          break;
        }

        size_t num_events;
        memcpy(&num_events, list, sizeof(size_t));
        int size = 0;
        printf("num events: %lu\n", num_events);

        // Cria mensagem a ser passada ao cliente pela pipe
        size = sizeof(int) + sizeof(size_t) + num_events * sizeof(unsigned int);
        printf("fez malloc\n");
        if (num_events != 0) {
          message_list = malloc(sizeof(int) + sizeof(size_t) + num_events * sizeof(unsigned int));
          // DÁ  ERRO
          printf("passou malloc\n");
          memcpy(message_list + sizeof(int), list, sizeof(size_t) + num_events * sizeof(unsigned int));
          memcpy(message_list, &response_val_list, sizeof(int));
          //memcpy(message + sizeof(int), &num_events, sizeof(size_t));
        }
        
        // Retorna valor ao cliente pela response pipe
        resp_fd_list = open(session->resp_pipe_path, O_WRONLY);
        if (resp_fd_list == -1) {
          fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
          return 1;
        }
        print_str_size(resp_fd_list, message_list, size);
        close(resp_fd_list);
        free(list);
        free(message_list);
        
        break;
      
      case EOC:
        printf("entrou EOC\n");
        ems_terminate();
        //return 1;
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