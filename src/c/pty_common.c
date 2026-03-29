#include "pty_common.h"

#ifdef _WIN32
#include <windows.h>

void pty_write(PtyHandle pipe_in, const char *buf, size_t len)
{
    while (len > 0) {
        DWORD written = 0;
        if (!WriteFile(pipe_in, buf, (DWORD)len, &written, NULL) || written == 0)
            break;
        buf += written;
        len -= (size_t)written;
    }
}
#else
#include <errno.h>
#include <unistd.h>

void pty_write(PtyHandle pty_fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = write(pty_fd, buf, len);
        if (n > 0) {
            buf += n;
            len -= (size_t)n;
        } else if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
    }
}
#endif
