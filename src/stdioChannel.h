#ifndef STDIO_CHANNEL_H_
#define STDIO_CHANNEL_H_

#include "lauxlib.h"
#include "stream.h"

#define STDIO_STDIN 0
#define STDIO_STDOUT 1
#define STDIO_STDERR 2

typedef enum stdioChannelKind {
  STDIO_CHANNEL_INHERIT_KIND,
  STDIO_CHANNEL_STREAM_KIND,
  STDIO_CHANNEL_EXTERNAL_STREAM_KIND,
  STDIO_CHANNEL_EXTERNAL_FILE_KIND,
  STDIO_CHANNEL_EXTERNAL_PATH_KIND,
  STDIO_CHANNEL_IGNORE_KIND
} stdioChannelKind;

typedef struct stdioChannel {
  stdioChannelKind kind;
  ELI_STREAM *stream;
  int fdToClose;
  luaL_Stream *file;
  const char *path;
} stdioChannel;
#endif
