#ifndef GHOSTLING_TERMINAL_UI_H
#define GHOSTLING_TERMINAL_UI_H

#include "pty_common.h"
#include "config_font.h"
#include <ghostty/vt.h>
#include "raylib.h"

void log_build_info(void);
bool handle_mouse(PtyHandle pty_fd, GhosttyMouseEncoder encoder,
                  GhosttyMouseEvent event, GhosttyTerminal terminal,
                  int cell_width, int cell_height, int pad_left, int pad_top,
                  int pad_right, int pad_bottom, int screen_width,
                  int screen_height);
bool handle_input(PtyHandle pty_fd, GhosttyKeyEncoder encoder,
                  GhosttyKeyEvent event, GhosttyTerminal terminal);
bool handle_scrollbar(GhosttyTerminal terminal, GhosttyRenderState render_state,
                      bool *dragging, int grid_origin_x, int grid_origin_y,
                      uint16_t term_rows, int cell_height, int pad_right);
bool copy_viewport_selection_to_clipboard(GhosttyTerminal terminal,
                                          uint16_t term_cols,
                                          uint16_t term_rows,
                                          uint16_t sel_x0,
                                          uint16_t sel_y0,
                                          uint16_t sel_x1,
                                          uint16_t sel_y1);
bool paste_host_clipboard_to_terminal(PtyHandle pty_fd, GhosttyTerminal terminal);
bool cell_has_hyperlink(GhosttyTerminal terminal, uint16_t x, uint16_t y);
bool open_url_at_cell(GhosttyTerminal terminal, uint16_t term_cols,
                      uint16_t term_rows, uint16_t x, uint16_t y);
/** @param font_size Logical point size for DrawTextEx (matches config); atlas is
 *  loaded larger on HiDPI — must stay logical here or cells and glyphs misalign. */
GhostlingHanTier render_terminal(
    GhosttyRenderState render_state, GhosttyRenderStateRowIterator row_iter,
    GhosttyRenderStateRowCells cells, Font font, int cell_width, int cell_height,
    int font_size, const GhosttyTerminalScrollbar *scrollbar, int grid_origin_x,
    int grid_origin_y, uint16_t term_rows, int pad_right, bool selection_active,
    uint16_t sel_x0, uint16_t sel_y0, uint16_t sel_x1, uint16_t sel_y1,
    GhostlingHanTier current_han_tier);

#endif
