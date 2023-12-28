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

struct Client* client = NULL; // Cliente

int ems_setup(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path) {
  //TODO: create pipes and connect to the server
  if (mkfifo(req_pipe_path, 0640) != 0 || mkfifo(resp_pipe_path, 0640) != 0) {
    fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
    return 1;
  }

  client = malloc(sizeof(struct Client));
  // malloc a paths das pipes
  if (client == NULL) {
    fprintf(stderr, "Error allocating memory for client\n");
    return 1;
  }
  strcpy(client->req_pipe_path, req_pipe_path);
  strcpy(client->resp_pipe_path, resp_pipe_path);
  client->session_id = 1;

  char message[strlen(req_pipe_path) + strlen(resp_pipe_path) + 3];
  sprintf(message, "%s %s", req_pipe_path, resp_pipe_path);

  // Conectar ao servidor -> escrever no pipe do server
  int server_fd = open(server_pipe_path, O_WRONLY);
  if (server_fd == -1) {
    fprintf(stderr, "[ERR]: open server pipe failed: %s\n", strerror(errno));
    return 1;
  }
  print_str(server_fd, message);


  // Abrir pipe de request para escrita e pipe de response para leitura
  /*
  int read_resp_fd = open(resp_pipe_path, O_RDONLY);
  if (read_resp_fd == -1) {
    fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
    return 1;
  }

  int write_req_fd = open(req_pipe_path, O_WRONLY);
  if (write_req_fd == -1) {
    fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
    return 1;
  }
  */

  // Associar session id ao named pipe do server
  return 0;
}

int ems_quit(void) { 
  //TODO: close pipes
  if (unlink(client->req_pipe_path) != 0 && errno != ENOENT) {
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", client->req_pipe_path, strerror(errno));
    return 1;
  };
  if (unlink(client->resp_pipe_path) != 0 && errno != ENOENT) {
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", client->resp_pipe_path, strerror(errno));
    return 1;
  };
  
  free(client);

  // falta dar free a paths das pipes
  // return 1 em caso de erro
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  //TODO: send create request to the server (through the request pipe) and wait for the response (through the response pipe)
  return 1;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  //TODO: send reserve request to the server (through the request pipe) and wait for the response (through the response pipe)
  return 1;
}

int ems_show(int out_fd, unsigned int event_id) {
  //TODO: send show request to the server (through the request pipe) and wait for the response (through the response pipe)
  return 1;
}

int ems_list_events(int out_fd) {
  //TODO: send list request to the server (through the request pipe) and wait for the response (through the response pipe)
  return 1;
}
