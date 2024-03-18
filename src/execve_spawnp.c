#ifndef _WIN32
#include "execve_spawnp.h"

int
execve_spawnp(const char* file, char* const argv[], char* const envp[]) {
    const char *p, *z, *path_env = getenv("PATH");
    int seen_eacces = 0;

    errno = ENOENT;
    if (!*file) {
        return -1;
    }

    if (strchr(file, '/') != NULL) {
        return execve(file, argv, envp);
    }

    if (!path_env) {
        path_env = "/usr/local/bin:/bin:/usr/bin";
    }
    if (strnlen(file, NAME_MAX + 1) > NAME_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }

    char* path = strdup(path_env);
    char* token = strtok(path, ":");
    while (token != NULL) {
        char* full_path = malloc(strlen(token) + strlen(file) + 2);
        strcpy(full_path, token);
        strcat(full_path, "/");
        strcat(full_path, file);

        execve(full_path, argv, envp);
        switch (errno) {
            case EACCES: seen_eacces = 1;
            case ENOENT:
            case ENOTDIR: break;
            default:
                free(full_path);
                free(path);
                return -1;
        }
        free(full_path);
        token = strtok(NULL, ":");
    }
    free(path);

    if (seen_eacces) {
        errno = EACCES;
    }
    return -1;
}

#endif