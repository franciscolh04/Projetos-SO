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

  int server_fd = open(argv[1], O_RDWR);
  if (server_fd == -1) {
    fprintf(stderr, "[ERR]: open server pipe failed: %s\n", strerror(errno));
    ems_terminate();
    return 1;
  }
  // Lê paths de pipes de request e response e conecta-se a eles
  char buffer[83]; // MAX SIZE OF 2 PATHS (verificar depois)

  while (1) {
    //TODO: Read from pipe
    ssize_t bytesRead = read(server_fd, buffer, sizeof(buffer));
    if (bytesRead == -1) {
      fprintf(stderr, "[ERR]: read from server pipe failed: %s\n", strerror(errno));
      close(server_fd);
      ems_terminate();
      return 1;
    }
    printf("%s\n", buffer);

    //TODO: Write new client to the producer-consumer buffer
    //break;
  }

  //TODO: Close Server
  if (close(server_fd) == -1) {
    fprintf(stderr, "[ERR]: close server pipe failed: %s\n", strerror(errno));
    ems_terminate();
    return 1;
  }

  ems_terminate();
}