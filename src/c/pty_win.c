#ifdef _WIN32

#include "pty_win.h"
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void win_perror(const char *prefix)
{
    DWORD err = GetLastError();
    char *msg = NULL;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err, 0, (char *)&msg, 0, NULL);
    if (msg) {
        fprintf(stderr, "%s: %s (error %lu)\n", prefix, msg, (unsigned long)err);
        LocalFree(msg);
    } else {
        fprintf(stderr, "%s: error %lu\n", prefix, (unsigned long)err);
    }
}

bool pty_spawn_win32(PtyContext *ctx, uint16_t cols, uint16_t rows,
                     const char *shell_override)
{
    HANDLE pipe_child_read = INVALID_HANDLE_VALUE;
    HANDLE pipe_child_write = INVALID_HANDLE_VALUE;
    HANDLE pipe_our_read = INVALID_HANDLE_VALUE;
    HANDLE pipe_our_write = INVALID_HANDLE_VALUE;
    LPPROC_THREAD_ATTRIBUTE_LIST attr_list = NULL;
    bool attr_list_initialized = false;
    HPCON hpc = INVALID_HANDLE_VALUE;

    memset(ctx, 0, sizeof(*ctx));
    ctx->hpc = INVALID_HANDLE_VALUE;
    ctx->process = INVALID_HANDLE_VALUE;
    ctx->pipe_in = INVALID_HANDLE_VALUE;
    ctx->pipe_out = INVALID_HANDLE_VALUE;

    /* MSDN pseudoconsole sample: CreatePipe(..., NULL, 0) — non-inheritable pipe handles. */
    if (!CreatePipe(&pipe_child_read, &pipe_our_write, NULL, 0)) {
        win_perror("CreatePipe (child stdin)");
        goto cleanup;
    }
    if (!CreatePipe(&pipe_our_read, &pipe_child_write, NULL, 0)) {
        win_perror("CreatePipe (child stdout)");
        goto cleanup;
    }

    COORD con_size = {.X = (SHORT)cols, .Y = (SHORT)rows};
    HRESULT hr = CreatePseudoConsole(con_size, pipe_child_read,
                                     pipe_child_write, 0, &hpc);
    if (FAILED(hr)) {
        fprintf(stderr, "CreatePseudoConsole failed: HRESULT 0x%08lX\n",
                (unsigned long)hr);
        goto cleanup;
    }

    CloseHandle(pipe_child_read);
    pipe_child_read = INVALID_HANDLE_VALUE;
    CloseHandle(pipe_child_write);
    pipe_child_write = INVALID_HANDLE_VALUE;

    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    attr_list = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(
        GetProcessHeap(), 0, attr_size);
    if (!attr_list) {
        win_perror("HeapAlloc (attribute list)");
        goto cleanup;
    }
    if (!InitializeProcThreadAttributeList(attr_list, 1, 0, &attr_size)) {
        win_perror("InitializeProcThreadAttributeList");
        goto cleanup;
    }
    attr_list_initialized = true;
    if (!UpdateProcThreadAttribute(attr_list, 0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc,
                                   sizeof(HPCON), NULL, NULL)) {
        win_perror("UpdateProcThreadAttribute (PSEUDOCONSOLE)");
        goto cleanup;
    }

    STARTUPINFOEXW si;
    memset(&si, 0, sizeof(si));
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    si.lpAttributeList = attr_list;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = NULL;
    si.StartupInfo.hStdOutput = NULL;
    si.StartupInfo.hStdError = NULL;

    wchar_t shell_cmd[MAX_PATH];
    wchar_t search_buf[MAX_PATH];

    if (shell_override && shell_override[0] != '\0') {
        if (!MultiByteToWideChar(CP_UTF8, 0, shell_override, -1, shell_cmd,
                                 MAX_PATH)) {
            win_perror("MultiByteToWideChar (--shell)");
            goto cleanup;
        }
    } else if (SearchPathW(NULL, L"pwsh.exe", NULL, MAX_PATH, search_buf,
                           NULL)) {
        wcscpy_s(shell_cmd, MAX_PATH, search_buf);
    } else if (SearchPathW(NULL, L"powershell.exe", NULL, MAX_PATH, search_buf,
                           NULL)) {
        wcscpy_s(shell_cmd, MAX_PATH, search_buf);
    } else {
        wcscpy_s(shell_cmd, MAX_PATH, L"cmd.exe");
    }

    SetEnvironmentVariableA("TERM", "xterm-256color");

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    /* Non-inheritable pipes: match Ghostty ConPTY (TRUE) without duplicating our pipe ends into the child. */
    if (!CreateProcessW(NULL, shell_cmd, NULL, NULL, TRUE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                        NULL, NULL, &si.StartupInfo, &pi)) {
        win_perror("CreateProcessW");
        goto cleanup;
    }
    CloseHandle(pi.hThread);

    DWORD spawn_code = 0;
    if (!GetExitCodeProcess(pi.hProcess, &spawn_code)) {
        win_perror("GetExitCodeProcess");
        CloseHandle(pi.hProcess);
        goto cleanup;
    }
    if (spawn_code != STILL_ACTIVE) {
        fprintf(stderr,
                "pty_spawn_win32: child exited immediately (exit code %lu)\n",
                (unsigned long)spawn_code);
        CloseHandle(pi.hProcess);
        goto cleanup;
    }

    DeleteProcThreadAttributeList(attr_list);
    HeapFree(GetProcessHeap(), 0, attr_list);
    attr_list = NULL;

    ctx->hpc = hpc;
    ctx->process = pi.hProcess;
    ctx->pipe_in = pipe_our_write;
    ctx->pipe_out = pipe_our_read;

    return true;

cleanup:
    if (attr_list) {
        if (attr_list_initialized)
            DeleteProcThreadAttributeList(attr_list);
        HeapFree(GetProcessHeap(), 0, attr_list);
    }
    if (hpc != INVALID_HANDLE_VALUE)
        ClosePseudoConsole(hpc);
    if (pipe_child_read != INVALID_HANDLE_VALUE)
        CloseHandle(pipe_child_read);
    if (pipe_child_write != INVALID_HANDLE_VALUE)
        CloseHandle(pipe_child_write);
    if (pipe_our_read != INVALID_HANDLE_VALUE)
        CloseHandle(pipe_our_read);
    if (pipe_our_write != INVALID_HANDLE_VALUE)
        CloseHandle(pipe_our_write);
    return false;
}

DWORD WINAPI pty_reader_thread(LPVOID param)
{
    PtyReadBuf *rb = (PtyReadBuf *)param;
    uint8_t tmp[4096];
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(rb->pipe, tmp, sizeof(tmp), &n, NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_BROKEN_PIPE && err != ERROR_NO_DATA &&
                err != ERROR_INVALID_HANDLE)
                win_perror("ReadFile (pty output)");
            EnterCriticalSection(&rb->cs);
            rb->eof = true;
            LeaveCriticalSection(&rb->cs);
            return 0;
        }
        /* TRUE with n==0: other end wrote 0 bytes (MSDN Pipes); not EOF — keep reading. */
        if (n == 0) {
            Sleep(1);
            continue;
        }
        DWORD written = 0;
        while (written < n) {
            EnterCriticalSection(&rb->cs);
            size_t space = PTY_BUF_SIZE - rb->len;
            if (space > 0) {
                size_t chunk =
                    (n - written) < space ? (n - written) : space;
                memcpy(rb->data + rb->len, tmp + written, chunk);
                rb->len += chunk;
                written += (DWORD)chunk;
            }
            LeaveCriticalSection(&rb->cs);
            if (written < n)
                Sleep(1);
        }
    }
}

PtyReadResult pty_buf_drain(PtyReadBuf *rb, PtyOutputSink sink,
                            void *sink_userdata)
{
    uint8_t local[PTY_BUF_SIZE];
    size_t count = 0;
    bool is_eof = false;

    EnterCriticalSection(&rb->cs);
    count = rb->len;
    if (count > 0) {
        memcpy(local, rb->data, count);
        rb->len = 0;
    }
    is_eof = rb->eof;
    LeaveCriticalSection(&rb->cs);

    if (count > 0) {
        if (sink)
            sink(sink_userdata, local, count);
    }

    if (count == 0 && is_eof)
        return PTY_READ_EOF;
    return PTY_READ_OK;
}

void pty_resize_win32(HPCON hpc, uint16_t cols, uint16_t rows)
{
    COORD size = {.X = (SHORT)cols, .Y = (SHORT)rows};
    HRESULT hr = ResizePseudoConsole(hpc, size);
    if (FAILED(hr))
        fprintf(stderr, "ResizePseudoConsole failed: HRESULT 0x%08lX\n",
                (unsigned long)hr);
}

void pty_cleanup_win(PtyContext *ctx)
{
    if (ctx->process != INVALID_HANDLE_VALUE) {
        if (WaitForSingleObject(ctx->process, 1000) != WAIT_OBJECT_0)
            TerminateProcess(ctx->process, 1);
        CloseHandle(ctx->process);
        ctx->process = INVALID_HANDLE_VALUE;
    }

    if (ctx->pipe_in != INVALID_HANDLE_VALUE) {
        CloseHandle(ctx->pipe_in);
        ctx->pipe_in = INVALID_HANDLE_VALUE;
    }
    if (ctx->pipe_out != INVALID_HANDLE_VALUE) {
        CloseHandle(ctx->pipe_out);
        ctx->pipe_out = INVALID_HANDLE_VALUE;
    }
}

#endif /* _WIN32 */
