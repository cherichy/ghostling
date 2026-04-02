#ifndef GHOSTLING_PTY_UNIX_H
#define GHOSTLING_PTY_UNIX_H

#include "pty_common.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef void (*PtyOutputSink)(void *userdata, const uint8_t *data, size_t len);

int pty_spawn_unix(pid_t *child_out, uint16_t cols, uint16_t rows,
                   const char *shell_override, int cell_width, int cell_height);
PtyReadResult pty_read_unix(int pty_fd, PtyOutputSink sink, void *sink_userdata);
void pty_resize_unix(int pty_fd, uint16_t cols, uint16_t rows);

#endif
