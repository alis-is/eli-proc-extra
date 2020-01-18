typedef struct ELI_PIPE
{
#ifdef _WIN32
    HANDLE h;
#else
    int fd;
#endif
    int closed;
    int nonblocking;
    const char *mode;
} ELI_PIPE;

#define PIPE_METATABLE "ELI_PIPE"
