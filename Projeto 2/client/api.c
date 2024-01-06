#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include "api.h"
#include "common/io.h"
#include "common/constants.h"

struct Client* client = NULL; // Cliente

int ems_setup(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path) {
  //TODO: create pipes and connect to the server
  if (mkfifo(req_pipe_path, 0640) != 0 || mkfifo(resp_pipe_path, 0640) != 0) {
    fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
    return 1;
  }

  client = malloc(sizeof(struct Client));
  if (client == NULL) {
    fprintf(stderr, "Error allocating memory for client\n");
    return 1;
  }
  strcpy(client->req_pipe_path, req_pipe_path);
  strcpy(client->resp_pipe_path, resp_pipe_path);

  char message[MAX_SIZE_PATHS]; // 1 + 40 + 40 + 1
  message[0] = '1';
  strncpy(&message[1], req_pipe_path, 40);
  strncpy(&message[41], resp_pipe_path, 40);

  // Conectar ao servidor -> escrever no pipe do server
  int server_fd = open(server_pipe_path, O_WRONLY);
  if (server_fd == -1) {
    fprintf(stderr, "[ERR]: open server pipe failed: %s\n", strerror(errno));
    return 1;
  }
  if (print_str_size(server_fd, message, 82)) {
    fprintf(stderr, "Error writing to server pipe\n");
    return 1;
  }
  if(close(server_fd) == -1) {
    fprintf(stderr, "Error closing server pipe\n");
    return 1;
  }

  // Abrir pipe de response para leitura
  int resp_fd = open(resp_pipe_path, O_RDONLY);
  if (resp_fd == -1) {
    fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
    return 1;
  }
  char response_setup[sizeof(int)];

  // Ler o session_id do response pipe
  ssize_t bytesRead = read(resp_fd, response_setup, sizeof(int));
  if (bytesRead == -1) {
    fprintf(stderr, "[ERR]: read from response pipe failed: %s\n", strerror(errno));
    if (close(resp_fd) == -1) {
      fprintf(stderr, "Error closing response pipe\n");
      return 1;
    }
    ems_quit();
    return 1;
  }
  if (close(resp_fd) == -1) {
    fprintf(stderr, "Error closing response pipe\n");
    return 1;
  }

  // Associar session id ao named pipe do server
  int session_id;
  memcpy(&session_id, response_setup, sizeof(int));
  client->session_id = session_id;

  return 0;
}

int ems_quit(void) {
  //TODO: send create request to the server (through the request pipe)
  char message[1];
  message[0] = '2';

  // Open request pipe to send
  int req_fd = open(client->req_pipe_path, O_WRONLY);
  if (req_fd == -1) {
    fprintf(stderr, "[ERR]: open server pipe failed: %s\n", strerror(errno));
    return 1;
  }
  if (print_str_size(req_fd, message, 1)) {
    fprintf(stderr, "Error writing to request pipe\n");
    return 1;
  }
  if(close(req_fd) == -1) {
    fprintf(stderr, "Error request pipe\n");
    return 1;
  }

  //TODO: close pipes
  if (unlink(client->resp_pipe_path) != 0 && errno != ENOENT) {
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", client->resp_pipe_path, strerror(errno));
    return 1;
  };
  if (unlink(client->req_pipe_path) != 0 && errno != ENOENT) {
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", client->req_pipe_path, strerror(errno));
    return 1;
  };
  
  free(client);
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  //TODO: send create request to the server (through the request pipe) and wait for the response (through the response pipe)
  char message[1 + sizeof(unsigned int) + 2 * sizeof(size_t)];
  message[0] = '3';

  memcpy(&message[1], &event_id, sizeof(unsigned int));
  memcpy(&message[1 + sizeof(unsigned int)], &num_rows, sizeof(size_t));
  memcpy(&message[1 + sizeof(unsigned int) + sizeof(size_t)], &num_cols, sizeof(size_t));

  // Open request pipe to send
  int req_fd = open(client->req_pipe_path, O_WRONLY);
  if (req_fd == -1) {
    fprintf(stderr, "[ERR]: open server pipe failed: %s\n", strerror(errno));
    return 1;
  }
  if (print_str_size(req_fd, message, 1 + sizeof(unsigned int) + 2 * sizeof(size_t))) {
    fprintf(stderr, "Error writing to request pipe\n");
    return 1;
  }
  if(close(req_fd) == -1) {
    fprintf(stderr, "Error request pipe\n");
    return 1;
  }

  // Open response pipe to receive
  int resp_fd = open(client->resp_pipe_path, O_RDONLY);
  if (resp_fd == -1) {
    fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
    return 1;
  }

  // Read from pipe
  char response[sizeof(int)];
  ssize_t bytesRead = read(resp_fd, response, sizeof(response));
  if (bytesRead == -1) {
    fprintf(stderr, "[ERR]: read from response pipe failed: %s\n", strerror(errno));
    if (close(resp_fd) == -1) {
      fprintf(stderr, "Error response pipe\n");
      return 1;
    }
    return 1;
  }
  if (close(resp_fd) == -1) {
    fprintf(stderr, "Error response pipe\n");
    return 1;
  }

  int response_val;
  memcpy(&response_val, &response, sizeof(int));

  return response_val;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  //TODO: send reserve request to the server (through the request pipe) and wait for the response (through the response pipe)
  char message[1 + sizeof(unsigned int) + sizeof(size_t) + 2 * (num_seats * sizeof(size_t))];
  message[0] = '4';

  memcpy(&message[1], &event_id, sizeof(unsigned int));
  memcpy(&message[1 + sizeof(unsigned int)], &num_seats, sizeof(size_t));
  memcpy(&message[1 + sizeof(unsigned int) + sizeof(size_t)], xs, num_seats * sizeof(size_t));
  memcpy(&message[1 + sizeof(unsigned int) + (num_seats + 1) * sizeof(size_t)], ys, num_seats * sizeof(size_t));

  // Open request pipe to send
  int req_fd = open(client->req_pipe_path, O_WRONLY);
  if (req_fd == -1) {
    fprintf(stderr, "[ERR]: open server pipe failed: %s\n", strerror(errno));
    return 1;
  }
  if (print_str_size(req_fd, message, 1 + sizeof(unsigned int) + sizeof(size_t) + 2 * (num_seats * sizeof(size_t)))) {
    fprintf(stderr, "Error writing to request pipe\n");
    return 1;
  }
  if(close(req_fd) == -1) {
    fprintf(stderr, "Error request pipe\n");
    return 1;
  }

  // Open response pipe to receive
  int resp_fd = open(client->resp_pipe_path, O_RDONLY);
  if (resp_fd == -1) {
    fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
    return 1;
  }

  // Read from pipe
  char response[sizeof(int)];
  ssize_t bytesRead = read(resp_fd, response, sizeof(response));
  if (bytesRead == -1) {
    fprintf(stderr, "[ERR]: read from response pipe failed: %s\n", strerror(errno));
    if(close(resp_fd) == -1) {
      fprintf(stderr, "Error response pipe\n");
      return 1;
    }
    return 1;
  }
  if (close(resp_fd) == -1) {
    fprintf(stderr, "Error response pipe\n");
    return 1;
  }

  int response_val;
  memcpy(&response_val, &response, sizeof(int));

  return response_val;
}

int ems_show(int out_fd, unsigned int event_id) {
  //TODO: send show request to the server (through the request pipe) and wait for the response (through the response pipe)
  char message[1 + sizeof(unsigned int)];
  message[0] = '5';

  memcpy(&message[1], &event_id, sizeof(unsigned int));

  // Open request pipe to send
  int req_fd = open(client->req_pipe_path, O_WRONLY);
  if (req_fd == -1) {
    fprintf(stderr, "[ERR]: open request pipe failed: %s\n", strerror(errno));
    return 1;
  }
  if (print_str_size(req_fd, message, 1 + sizeof(unsigned int))) {
    fprintf(stderr, "Error writing to request pipe\n");
    return 1;
  }
  if (close(req_fd) == -1) {
    fprintf(stderr, "Error request pipe\n");
    return 1;
  }

  // Open response pipe to receive
  int resp_fd = open(client->resp_pipe_path, O_RDONLY);
  if (resp_fd == -1) {
    fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
    return 1;
  }

  // Read from pipe
  char *response = malloc(sizeof(int) + 2 * sizeof(size_t));
  if (response == NULL) {
    fprintf(stderr, "Error allocating memory\n");
    return 1;
  }
  ssize_t bytesRead = read(resp_fd, response, sizeof(int) + 2 * sizeof(size_t));
  if (bytesRead == -1) {
    fprintf(stderr, "[ERR]: read from response pipe failed: %s\n", strerror(errno));
    if(close(resp_fd) == -1) {
      fprintf(stderr, "Error reponse pipe\n");
      return 1;
    }
    return 1;
  }
  int response_val;
  size_t num_rows, num_cols;
  memcpy(&response_val, response, sizeof(int));
  
  if(response_val) {
    free(response);
    return response_val;
  }
  memcpy(&num_rows, response + sizeof(int), sizeof(size_t));
  memcpy(&num_cols, response + (sizeof(int) + sizeof(size_t)), sizeof(size_t));

  free(response);
  response = malloc(num_rows * num_cols * sizeof(size_t));
  if(response == NULL) {
    fprintf(stderr, "Error allocating memory\n");
    return 1;
  }

  
  bytesRead = read(resp_fd, response, (num_rows * num_cols) * sizeof(unsigned int));
  if (bytesRead == -1) {
    fprintf(stderr, "[ERR]: read from response pipe failed: %s\n", strerror(errno));
    if(close(resp_fd) == -1) {
      fprintf(stderr, "Error response pipe\n");
      return 1;
    }
    return 1;
  }
  if (close(resp_fd) == -1) {
    fprintf(stderr, "Error response pipe\n");
    return 1;
  }

  // Write to output file
  for (size_t i = 1; i <= num_rows; i++) {
    for (size_t j = 1; j <= num_cols; j++) {
      char buffer[16];
      size_t a;
      memcpy(&a, response + (sizeof(size_t) * ((i - 1) * num_cols + j - 1)), sizeof(size_t));
      sprintf(buffer, "%lu", a);

      if (print_str(out_fd, buffer)) {
        perror("Error writing to file descriptor");
        return 1;
      }

      if (j < num_cols) {
        if (print_str(out_fd, " ")) {
          perror("Error writing to file descriptor");
          return 1;
        }
      }

    }
    if (print_str(out_fd, "\n")) {
      perror("Error writing to file descriptor");
      return 1;
    }

  }
  free(response);
  return response_val;
}

int ems_list_events(int out_fd) {
  //TODO: send list request to the server (through the request pipe) and wait for the response (through the response pipe)
  char message[1];
  message[0] = '6';

  // Open request pipe to send
  int req_fd = open(client->req_pipe_path, O_WRONLY);
  if (req_fd == -1) {
    fprintf(stderr, "[ERR]: open request pipe failed: %s\n", strerror(errno));
    return 1;
  }
  if (print_str_size(req_fd, message, 1)) {
    fprintf(stderr, "Error writing to request pipe\n");
    return 1;
  };
  if(close(req_fd) == -1) {
    fprintf(stderr, "Error request pipe\n");
    return 1;
  }

  // Open response pipe to receive
  int resp_fd = open(client->resp_pipe_path, O_RDONLY);
  if (resp_fd == -1) {
    fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
    return 1;
  }

  // Read from pipe
  char *response = malloc(sizeof(int) + sizeof(size_t));
  if (response == NULL) {
    fprintf(stderr, "Error allocating memory\n");
    return 1;
  }
  ssize_t bytesRead = read(resp_fd, response, sizeof(int) + sizeof(size_t));
  if (bytesRead == -1) {
    fprintf(stderr, "[ERR]: read from response pipe failed: %s\n", strerror(errno));
    if(close(resp_fd) == -1) {
      fprintf(stderr, "Error response pipe\n");
      return 1;
    }
    return 1;
  }
  int response_val;
  size_t num_events;
  memcpy(&response_val, response, sizeof(int));
  if(response_val) {
    free(response);
    return response_val;
  }
  memcpy(&num_events, response + sizeof(int), sizeof(size_t));

  free(response);
  response = malloc(num_events * sizeof(unsigned int));
  if (response == NULL) {
    fprintf(stderr, "Error allocating memory\n");
    return 1;
  }

  bytesRead = read(resp_fd, response, num_events * sizeof(unsigned int));
  if (bytesRead == -1) {
    fprintf(stderr, "[ERR]: read from response pipe failed: %s\n", strerror(errno));
    if (close(resp_fd) == -1) {
      fprintf(stderr, "Error response pipe\n");
      return 1;
    }
    return 1;
  }
  if(close(resp_fd) == -1) {
    fprintf(stderr, "Error response pipe\n");
    return 1;
  }

  // Write to output file
  if (num_events == 0) {
    if (print_str(out_fd, "No events\n")) {
      fprintf(stderr, "Error writing to stdout\n");
      return 1;
    }
  }
  else {
    for(size_t i = 0; i < num_events; i++) {
      if (print_str(out_fd, "Event ")) {
        fprintf(stderr, "Error writing to stdout\n");
        return 1;
      }
      unsigned int temp;
      memcpy(&temp, response + i * sizeof(unsigned int), sizeof(unsigned int));

      char id[16];
      sprintf(id, "%u\n", temp);
      if(print_str(out_fd, id)) {
        fprintf(stderr, "Error writing to stdout\n");
        return 1;
      }
    }
  }
  return response_val;
}