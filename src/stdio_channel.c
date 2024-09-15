#include <stdlib.h>
#include "stdio_channel.h"

#ifdef _WIN32
#include <stdio.h>
#include <windows.h>
#endif

stdio_channel*
new_stdio_channel() {
    stdio_channel* channel = calloc(1, sizeof(stdio_channel));
#ifdef _WIN32
    channel->fd_to_close = INVALID_HANDLE_VALUE;
#else
    channel->fd_to_close = -1;
#endif
    return channel;
}

void
close_stdio_channel_to_close(stdio_channel* channel) {
    if (channel == NULL) {
        return;
    }

#ifdef _WIN32
    if (channel->fd_to_close != INVALID_HANDLE_VALUE) {
        CloseHandle(channel->fd_to_close);
        channel->fd_to_close = INVALID_HANDLE_VALUE;
    }
#else
    if (channel->fd_to_close >= 0) {
        close(channel->fd_to_close);
        channel->fd_to_close = -1;
    }
#endif
}

void
close_stdio_channel(stdio_channel* channel) {
    if (channel == NULL) {
        return;
    }
    switch (channel->kind) {
        case STDIO_CHANNEL_STREAM_KIND:
#ifdef _WIN32
            if (channel->stream->fd != INVALID_HANDLE_VALUE) {
                CloseHandle(channel->stream->fd);
            }
#else
            if (channel->stream->fd >= 0) {
                close(channel->stream->fd);
            }
#endif
            free(channel->stream);
            break;
        default: break;
    }
}

int
stdio_channel_clone_into_stream(stdio_channel* channel, ELI_STREAM* stream) {
    stream->closed = channel->stream->closed;
#ifdef _WIN32
    BOOL success = DuplicateHandle(GetCurrentProcess(), channel->stream->fd, GetCurrentProcess(), &stream->fd, 0, FALSE,
                                   DUPLICATE_SAME_ACCESS);
    if (!success) {
        return 0;
    }
#else
    stream->fd = dup(channel->stream->fd);
    if (stream->fd < 0) {
        return 0;
    }
#endif
    stream->not_disposable = 0;
    stream->nonblocking = channel->stream->nonblocking;
    return 1;
}