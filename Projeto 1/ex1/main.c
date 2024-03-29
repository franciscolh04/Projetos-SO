#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "eventlist.h"

#include "constants.h"
#include "operations.h"
#include "parser.h"

#define TRUE 1
#define FALSE 0

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

  if (argc > 2) {
    char *endptr;
    unsigned long int delay = strtoul(argv[2], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_ms = (unsigned int)delay;
  }

  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  char *dirpath = argv[1];

  DIR *dir = opendir(dirpath);
  if (dir == NULL) {
    fprintf(stderr, "Failed to open directory\n");
    return 1;
  }

  struct dirent *dp;

  while ((dp = readdir(dir)) != NULL) {
    int fdRead = 0;
    int fdWrite = 0;
    
    // Encontra os ficheiros com extensão ".jobs"
    if (strstr(dp->d_name, ".jobs") != NULL) {
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

      int flag = 1;
      while (flag == 1) {
        unsigned int event_id, delay;
        size_t num_rows, num_columns, num_coords;
        size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

        switch (get_next(fdRead)) {

          case CMD_CREATE:
            if (parse_create(fdRead, &event_id, &num_rows, &num_columns) != 0) {
              fprintf(stderr, "Invalid command. See HELP for usage\n");
              continue;
            }

            if (ems_create(event_id, num_rows, num_columns)) {
              fprintf(stderr, "Failed to create event\n");
            }

            break;

          case CMD_RESERVE:
            num_coords = parse_reserve(fdRead, MAX_RESERVATION_SIZE, &event_id, xs, ys);

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
              continue;
            }

            if (ems_show(fdWrite, event_id)) {
              fprintf(stderr, "Failed to show event\n");
            }

            break;

          case CMD_LIST_EVENTS:
            if (ems_list_events(fdWrite)) {
              fprintf(stderr, "Failed to list events\n");
            }

            break;

          case CMD_WAIT:
            if (parse_wait(fdRead, &delay, NULL) == -1) {  // thread_id is not implemented
              fprintf(stderr, "Invalid command. See HELP for usage\n");
              continue;
            }

            if (delay > 0) {
              printf("Waiting...\n");
              ems_wait(delay);
            }

            break;

          case CMD_INVALID:
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            break;

          case CMD_HELP:
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

          case CMD_BARRIER:  // Not implemented
          case CMD_EMPTY:
            break;

          case EOC:
            ems_free_all_events();
            ems_reset_event_list();

            if (close(fdRead) == -1) {
              fprintf(stderr,"Failed to close file\n");
              return 1;
            }

            if(close(fdWrite) == -1) {
              fprintf(stderr,"Failed to close file\n");
              return 1;
            }

            flag = 0;
        }
      }
    }
  }
  if (closedir(dir) == -1) {
    fprintf(stderr, "Failed to close directory\n");
    return 1;
  }
  return 0;
}
