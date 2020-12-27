#include "lua.h"
#include "pipe.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define STDIO_STDIN 0
#define STDIO_STDOUT 1
#define STDIO_STDERR 2

typedef enum stdioChannelKind {
    STDIO_CHANNEL_INHERIT_KIND,
    STDIO_CHANNEL_OWN_PIPE_KIND,
    STDIO_CHANNEL_PIPE_END_KIND, 
    STDIO_CHANNEL_FILE_KIND,
    STDIO_CHANNEL_IGNORE_KIND
} stdioChannelKind;

typedef struct stdioChannel {
    stdioChannelKind kind;
    ELI_PIPE_END *pipeEnd;
#ifdef _WIN32    
    
#else
    pid_t pid;
#endif
    luaL_Stream *file;
} stdioChannel;

typedef struct process
{
    int status;
#ifdef _WIN32
    HANDLE hProcess;
    DWORD dwProcessId;
#else
    pid_t pid;
#endif
    stdioChannel* stdio[3];
} process;

#define PROCESS_METATABLE "ELI_PROCESS"

int process_create_meta(lua_State *L);
