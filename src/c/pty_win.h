#ifndef GHOSTLING_PTY_WIN_H
#define GHOSTLING_PTY_WIN_H

#include "pty_common.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#endif

typedef void (*PtyOutputSink)(void *userdata, const uint8_t *data, size_t len);

typedef struct {
    HPCON hpc;
    HANDLE process;
    HANDLE pipe_in;
    HANDLE pipe_out;
} PtyContext;

#define PTY_BUF_SIZE 65536

typedef struct {
    CRITICAL_SECTION cs;
    uint8_t data[PTY_BUF_SIZE];
    size_t len;
    bool eof;
    HANDLE pipe;
} PtyReadBuf;

void win_perror(const char *prefix);
bool pty_spawn_win32(PtyContext *ctx, uint16_t cols, uint16_t rows,
                     const char *shell_override);
DWORD WINAPI pty_reader_thread(LPVOID param);
PtyReadResult pty_buf_drain(PtyReadBuf *rb, PtyOutputSink sink,
                            void *sink_userdata);
void pty_resize_win32(HPCON hpc, uint16_t cols, uint16_t rows);
void pty_cleanup_win(PtyContext *ctx);

#endif
