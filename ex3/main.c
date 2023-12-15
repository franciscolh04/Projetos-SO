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

#include <sys/wait.h>

#define TRUE 1
#define FALSE 0

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

  if (argc > 4) {
    char *endptr;
    unsigned long int delay = strtoul(argv[4], &endptr, 10);

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
  int MAX_PROC = atoi(argv[2]);
  int MAX_THREADS = atoi(argv[3]);

  DIR *dir = opendir(dirpath);
  if (dir == NULL) {
    fprintf(stderr, "Failed to open directory\n");
    return 1;
  }

  struct dirent *dp;

  while ((dp = readdir(dir)) != NULL) {
    
    // Encontra os ficheiros com extensão ".jobs"
    if (strstr(dp->d_name, ".jobs") != NULL) {
      static int activeProcesses = 0;
      pid_t pid = fork();
       
      if(pid == -1) {
        fprintf(stderr, "Failed to create a child process\n");
        exit(EXIT_FAILURE);
      }

      else if (pid == 0) {
       
        ems_execute_child(dp, dirpath, MAX_THREADS);
        exit(EXIT_SUCCESS);
      }

      if (pid > 0) {
        
        activeProcesses++;
        //Espera a conclusão do processo filho se atingir o número máximo de processos ativos
        if(activeProcesses >= MAX_PROC) {
          int status;
          pid_t terminated_pid = wait(&status);

          if (WIFEXITED(status)) {
              printf("Processo filho %d terminado com estado %d\n", terminated_pid, WEXITSTATUS(status));
          } else if (WIFSIGNALED(status)) {
              printf("Processo filho %d terminado por signal %d\n", terminated_pid, WTERMSIG(status));
          }

          activeProcesses--;
        }
      }
    }

    // Espera a conclusão dos processos filhos restantes
    int status = 0;
    pid_t terminated_pid;
    while ((terminated_pid = wait(&status)) > 0) {
        if (WIFEXITED(status)) {
            printf("Processo filho %d terminado com estado %d\n", terminated_pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Processo filho %d terminado por signal %d\n", terminated_pid, WTERMSIG(status));
        }
    }
  }
  closedir(dir);
  return 0;
}