#ifndef _WIN32
#ifndef ELI_EXECVPE_H_
#define ELI_EXECVPE_H_
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int execve_spawnp(const char* file, char* const argv[], char* const envp[]);

#endif // ELI_EXECVPE_H_
#endif