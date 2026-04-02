#ifndef GHOSTLING_TABS_H
#define GHOSTLING_TABS_H

#include "raylib.h"

#include "agent_state.h"
#include "effects.h"
#include "osc52_clipboard.h"
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
#define TAB_STRIP_W_DEFAULT 156
#define TAB_STRIP_W_MIN 96
/** Collapsed: max horizontal extent used to center the expand chevron near the
 *  left edge (hit box is a small rect inside this, not the full band). */
#define TAB_COLLAPSED_EDGE_HOVER 20
#define TAB_SPLITTER_GRAB 8
#define TAB_NEW_H 44
#define TAB_CLOSE_W 28
/** Left gutter for 1..N index; spans full tab row height (title + reserved). */
#define TAB_INDEX_COL_W 28
/** Pass as @p edit_idx when no tab is being renamed. */
#define TAB_EDIT_NONE ((size_t)-1)

typedef struct Tab {
    bool in_use;
#ifdef _WIN32
    bool pty_cs_inited;
#endif
    GhosttyTerminal terminal;
    EffectsContext effects;
    Osc52ClipboardState osc52;
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
    GhostlingAgentState agent_state;

    /* Local mouse-selection state used for host-side clipboard copy. */
    bool selection_active;
    bool selection_dragging;
    uint16_t selection_anchor_x;
    uint16_t selection_anchor_y;
    uint16_t selection_focus_x;
    uint16_t selection_focus_y;

    void (*agent_state_hook)(void *userdata,
                             const struct Tab *tab,
                             const GhostlingAgentState *before,
                             const GhostlingAgentState *after);
    void *agent_state_hook_userdata;

    int osc1_filter_mode;
    bool osc1_filter_esc_pending;
    bool osc1_filter_cmd_decided;
    bool osc1_filter_drop;
    unsigned osc1_filter_cmd;
    size_t osc1_filter_cmd_digits;
    uint8_t osc1_filter_raw[512];
    size_t osc1_filter_raw_len;
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

void tab_display_title(const Tab *t, size_t tab_index_one_based, char *out,
                       size_t outsz);

void tab_set_agent_state_hook(
    Tab *t,
    void (*hook)(void *userdata, const Tab *tab,
                 const GhostlingAgentState *before,
                 const GhostlingAgentState *after),
    void *userdata);

void tab_agent_state_on_local_input(Tab *t);
void tab_agent_state_on_process_exit(Tab *t, int exit_status);

bool tab_splitter_hit(Vector2 mpos, int strip_w, int scr_h);

/** Hit test for the collapse/expand control on the splitter (center height). */
bool tab_splitter_toggle_hit(Vector2 mpos, int effective_strip_w, int scr_h);

bool tab_strip_hit(Vector2 mpos, int strip_w, int scr_h, size_t n_tabs,
                   size_t *idx, TabStripAction *act, int tab_title_h,
                   int tab_reserved_h, bool strip_collapsed);

void tab_strip_draw(Font font, float font_size, int strip_w, int scr_h,
                    Tab *const *tabs, size_t n_tabs, size_t active_idx,
                    size_t edit_idx, const char *edit_buf, Color strip_bg,
                    Color tab_index_bg, Color tab_reserved_bg, Color tab_bg,
                    Color tab_active, Color border, Color fg, Color edit_bg,
                    int tab_title_h, int tab_reserved_h, bool strip_collapsed);

/** Draw `<` / `>` on top of splitter highlight etc.; call after tab strip + splitter line. */
void tab_splitter_toggle_draw(Font font, float font_size, int strip_w, int scr_h,
                              bool strip_collapsed, bool show, Color fg);

#endif
