#ifndef GHOSTLING_PTY_UNIX_H
#define GHOSTLING_PTY_UNIX_H

#include "pty_common.h"
#include "osc52_clipboard.h"
#include <ghostty/vt.h>
#include <sys/types.h>

int pty_spawn_unix(pid_t *child_out, uint16_t cols, uint16_t rows,
                   const char *shell_override, int cell_width, int cell_height);
PtyReadResult pty_read_unix(int pty_fd, GhosttyTerminal terminal,
                            Osc52ClipboardState *osc52);
void pty_resize_unix(int pty_fd, uint16_t cols, uint16_t rows);

#endif
