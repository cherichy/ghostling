#ifndef GHOSTLING_TABS_H
#define GHOSTLING_TABS_H

#include "raylib.h"

#include "effects.h"
#include "pty_common.h"
#include <ghostty/vt.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include "pty_win.h"
#include <windows.h>
#else
#include <sys/types.h>
#endif

#define MAX_TABS 16
#define TAB_STRIP_W 156
#define TAB_ROW_H 36
#define TAB_NEW_H 44
#define TAB_CLOSE_W 28

typedef struct Tab {
    bool in_use;
#ifdef _WIN32
    bool pty_cs_inited;
#endif
    GhosttyTerminal terminal;
    EffectsContext effects;
#ifdef _WIN32
    PtyContext pty_ctx;
    PtyReadBuf pty_rb;
    HANDLE pty_reader;
#else
    pid_t child;
    int pty_fd;
#endif
    bool child_exited;
    bool child_reaped;
    int child_exit_status;
} Tab;

typedef enum {
    TAB_STRIP_NONE = 0,
    TAB_STRIP_SELECT,
    TAB_STRIP_CLOSE,
    TAB_STRIP_NEW,
} TabStripAction;

void tab_init_struct(Tab *t);
void tab_free(Tab *t);

bool tab_start_shell(Tab *t, uint16_t cols, uint16_t rows, int cell_width,
                     int cell_height, const char *shell_override);

void tab_bind_ghostty_callbacks(Tab *t);

void tab_resize_pty(Tab *t, uint16_t cols, uint16_t rows, int cell_width,
                    int cell_height);

PtyReadResult tab_drain(Tab *t);

PtyHandle tab_pty_write(Tab *t);

bool tab_strip_hit(Vector2 mpos, int scr_h, size_t n_tabs, size_t *idx,
                   TabStripAction *act);

void tab_strip_draw(Font font, float font_size, int scr_h, size_t n_tabs,
                    size_t active_idx, Color strip_bg, Color tab_bg,
                    Color tab_active, Color border, Color fg);

#endif
