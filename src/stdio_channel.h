#ifndef STDIO_CHANNEL_H_
#define STDIO_CHANNEL_H_

#include "lauxlib.h"
#include "stream.h"

#define STDIO_STDIN          0
#define STDIO_STDOUT         1
#define STDIO_STDERR         2
#define STDIO_OUTPUT_STREAMS 3

typedef enum stdio_channelKind {
    STDIO_CHANNEL_INHERIT_KIND,
    STDIO_CHANNEL_STREAM_KIND,
    STDIO_CHANNEL_EXTERNAL_STREAM_KIND,
    STDIO_CHANNEL_EXTERNAL_FILE_KIND,
    STDIO_CHANNEL_EXTERNAL_PATH_KIND,
    STDIO_CHANNEL_IGNORE_KIND
} stdio_channelKind;

typedef struct stdio_channel {
    stdio_channelKind kind;
    ELI_STREAM* stream;
#ifdef _WIN32
    HANDLE fd_to_close;
#else
    int fd_to_close;
#endif
    luaL_Stream* file;
    const char* path;
} stdio_channel;

stdio_channel* new_stdio_channel();
void close_stdio_channel_to_close(stdio_channel* channel);
void close_stdio_channel(stdio_channel* channel);
int stdio_channel_clone_into_stream(stdio_channel* channel, ELI_STREAM* stream);
#endif