#ifndef GHOSTLING_PTY_COMMON_H
#define GHOSTLING_PTY_COMMON_H

#include <stddef.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <windows.h>
typedef HANDLE PtyHandle;
#else
typedef int PtyHandle;
#endif

typedef enum {
    PTY_READ_OK,
    PTY_READ_EOF,
    PTY_READ_ERROR,
} PtyReadResult;

void pty_write(PtyHandle fd, const char *buf, size_t len);

#endif
