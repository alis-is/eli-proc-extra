#include "lua.h"
#include "stream.h"

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
    STDIO_CHANNEL_STREAM_KIND,
    STDIO_CHANNEL_EXTERNAL_STREAM_KIND,
    STDIO_CHANNEL_EXTERNAL_FILE_KIND,
    STDIO_CHANNEL_IGNORE_KIND
} stdioChannelKind;

typedef struct stdioChannel {
    stdioChannelKind kind;
    ELI_STREAM *stream;
    int fdToClose;
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
