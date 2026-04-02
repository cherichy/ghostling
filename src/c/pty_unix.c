#ifndef _WIN32

#include "pty_unix.h"
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

int pty_spawn_unix(pid_t *child_out, uint16_t cols, uint16_t rows,
                   const char *shell_override, int cell_width, int cell_height)
{
    int pty_fd;
    struct winsize ws = {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = (unsigned short)(cols * cell_width),
        .ws_ypixel = (unsigned short)(rows * cell_height),
    };

    pid_t child = forkpty(&pty_fd, NULL, NULL, &ws);
    if (child < 0) {
        perror("forkpty");
        return -1;
    }
    if (child == 0) {
        const char *shell = shell_override;
        if (!shell || shell[0] == '\0') {
            shell = getenv("SHELL");
            if (!shell || shell[0] == '\0') {
                struct passwd *pw = getpwuid(getuid());
                if (pw && pw->pw_shell && pw->pw_shell[0] != '\0')
                    shell = pw->pw_shell;
                else
                    shell = "/bin/sh";
            }
        }

        const char *shell_name = strrchr(shell, '/');
        shell_name = shell_name ? shell_name + 1 : shell;

        setenv("TERM", "xterm-256color", 1);
        execl(shell, shell_name, NULL);
        _exit(127);
    }

    int flags = fcntl(pty_fd, F_GETFL);
    if (flags < 0 || fcntl(pty_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl O_NONBLOCK");
        close(pty_fd);
        return -1;
    }

    *child_out = child;
    return pty_fd;
}

PtyReadResult pty_read_unix(int pty_fd, GhosttyTerminal terminal,
                            Osc52ClipboardState *osc52,
                            GhostlingAgentState *agent_state,
                            EffectsContext *effects)
{
    uint8_t buf[4096];
    for (;;) {
        ssize_t n = read(pty_fd, buf, sizeof(buf));
        if (n > 0) {
            osc52_clipboard_scan(osc52, buf, (size_t)n);
            ghostling_agent_state_feed_output(agent_state, buf, (size_t)n);
            effect_scan_icon_osc(effects, buf, (size_t)n);
            ghostty_terminal_vt_write(terminal, buf, (size_t)n);
        } else if (n == 0) {
            return PTY_READ_EOF;
        } else {
            if (errno == EAGAIN)
                return PTY_READ_OK;
            if (errno == EINTR)
                continue;
            if (errno == EIO)
                return PTY_READ_EOF;
            perror("pty read");
            return PTY_READ_ERROR;
        }
    }
}

void pty_resize_unix(int pty_fd, uint16_t cols, uint16_t rows)
{
    struct winsize ws = {.ws_row = rows, .ws_col = cols};
    ioctl(pty_fd, TIOCSWINSZ, &ws);
}

#endif /* !_WIN32 */
