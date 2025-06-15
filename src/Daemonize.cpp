#include "Daemonize.hpp"
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstdio>

void Daemonize::run(const char* pidFile) {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) exit(EXIT_FAILURE);
    umask(0);
    if (chdir("/") < 0) exit(EXIT_FAILURE);

    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) exit(EXIT_FAILURE);
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);

    if (pidFile) {
        FILE* f = fopen(pidFile, "w");
        if (f) {
            fprintf(f, "%d", getpid());
            fclose(f);
        }
    }
}