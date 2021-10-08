#include "myshell_parser.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define TRUE 1
#define STD_INPUT 0
#define STD_OUTPUT 1


void zombie_handler(int sig) {
    pid_t pid;
    int status;

    while ((pid = waitpid(0, &status, WNOHANG)) > 0) {};
}

/* start a process */
void start_process(struct pipeline_command* curr_command, int background) {
    pid_t pid;
    int status;
    int fd[2];
    struct sigaction act;

    if (pipe(fd) == -1) {
        perror("ERROR");
        exit(1);
    }
    pid = fork();
    if (pid < 0) {
        perror("ERROR");
        exit(1);
    }
    if (pid == 0) {   // child process
        if (curr_command->redirect_in_path != NULL) {
            if ((fd[0] = open(curr_command->redirect_in_path, O_RDONLY)) < 0) {
                perror("ERROR");
                exit(1);
            }
        }
        dup2(fd[0], STD_INPUT);
        close(fd[1]);
        close(fd[0]);
        if (curr_command->redirect_out_path != NULL) {
            if ((fd[1] = open(curr_command->redirect_out_path, O_WRONLY|O_CREAT|O_TRUNC, 0777)) < 0) {
                perror("ERROR");
                exit(1);
            }
        }
        dup2(fd[1], 1);
        close(fd[1]);

        if (execvp(curr_command->command_args[0], curr_command->command_args) < 0) {
            perror("ERROR");
            exit(1);
        }
    }
    else {  // parent process
      if (background) { // background process does not wait
          sigemptyset(&act.sa_mask);
          act.sa_flags = 0;
          act.sa_handler = zombie_handler;
          sigaction(SIGCHLD, &act, NULL);
        }
        else {   // wait until finished
            do {
                  waitpid(pid, &status, WUNTRACED);
            } while (!WIFEXITED(status) && !WIFSIGNALED(status));
        }
    }
}


void pipe_process(struct pipeline_command* command, int pipe_count) {
    pid_t pid;
    int status;
    int curr_fd[2];
    int next_fd[2];
    int command_count = 0;

    if (pipe(curr_fd) < 0) {
        perror("ERROR");
        exit(1);
    }

    while (command != NULL) {
      if (command->next != NULL) {
          if (pipe(next_fd) < 0) {
              perror("ERROR");
              exit(1);
          }
      }
      if ((pid = fork()) < 0) {
         perror("ERROR");
         exit(1);
      }
      if (pid == 0) {
          /* child gets input from the previous command,
          if it's not the first command */
          if (command_count != 0) {
              if (command->redirect_in_path != NULL) {
                  if ((curr_fd[0] = open(command->redirect_in_path, O_RDONLY)) < 0) {
                      perror("ERROR");
                      exit(1);
                  }
              }
              dup2(curr_fd[0], 0);
              close(curr_fd[0]);
              close(curr_fd[1]);
          }
          /* child outputs to next command,
          if it's not the last command */
          if (command_count < pipe_count + 1) {
              close(next_fd[0]);
              if (command->redirect_out_path != NULL) {
                  if ((next_fd[1] = open(command->redirect_out_path, O_WRONLY|O_CREAT|O_TRUNC, 0777)) < 0) {
                      perror("ERROR");
                      exit(1);
                  }
              }
              dup2(next_fd[1], 1);
              close(next_fd[1]);
          }

          if (execvp(command->command_args[0], command->command_args) < 0) {
              perror("ERROR");
              exit(1);
          }
      }
      else {
          if (command_count != 0) {
              close(curr_fd[0]);
              close(curr_fd[1]);
              waitpid(pid, &status, WUNTRACED);
          }
          if (command_count < pipe_count + 1) {
              curr_fd[0] = next_fd[0];
              curr_fd[1] = next_fd[1];
          }
      }
      command = command->next;
      command_count++;
    }

    close(curr_fd[0]);
    close(curr_fd[1]);
}

int main(int argc, char **argv) {
    struct pipeline* pipeline = malloc(sizeof(struct pipeline));
    struct pipeline_command* curr_command;
    char* command_line = malloc(MAX_LINE_LENGTH);
    int background = 0;
    int pipe_count = 0;

    while (TRUE) {
        /* display prompt/check for -n complile option */
        //int opt = getopt(argc, argv, "n");
        if (argc == 1) {
            printf("myshell$");
        }

        /* read input from terminal */
        if (fgets(command_line, MAX_LINE_LENGTH, stdin) == NULL && feof(stdin)) {
            break;
        }
        pipe_count = 0;

        /* build the pipeline */
        pipeline = pipeline_build(command_line);
        curr_command = pipeline->commands;
        while (curr_command->next != NULL) {
            pipe_count++;
            curr_command = curr_command->next;
        }
        curr_command = pipeline->commands;

        if (pipeline->is_background == TRUE) {
            background = 1;
        }

        while (curr_command != NULL) {
            if (curr_command->next != NULL) {
                pipe_process(curr_command, pipe_count);
                break;
            }
            else {
                start_process(curr_command, background);
                if (background) {
                    sleep(1);
                }
            }
            curr_command = curr_command->next;
        }
    }

    return 0;
}
