#ifndef GHOSTLING_TERMINAL_UI_H
#define GHOSTLING_TERMINAL_UI_H

#include "pty_common.h"
#include <ghostty/vt.h>
#include "raylib.h"

void log_build_info(void);
bool handle_mouse(PtyHandle pty_fd, GhosttyMouseEncoder encoder,
                  GhosttyMouseEvent event, GhosttyTerminal terminal,
                  int cell_width, int cell_height, int pad_left, int pad_top,
                  int pad_right, int pad_bottom);
bool handle_input(PtyHandle pty_fd, GhosttyKeyEncoder encoder,
                  GhosttyKeyEvent event, GhosttyTerminal terminal);
bool handle_scrollbar(GhosttyTerminal terminal, GhosttyRenderState render_state,
                      bool *dragging, int grid_origin_x, int grid_origin_y,
                      uint16_t term_rows, int cell_height, int pad_right);
/** @param font_size Logical point size for DrawTextEx (matches config); atlas is
 *  loaded larger on HiDPI — must stay logical here or cells and glyphs misalign. */
void render_terminal(GhosttyRenderState render_state,
                     GhosttyRenderStateRowIterator row_iter,
                     GhosttyRenderStateRowCells cells, Font font,
                     int cell_width, int cell_height, int font_size,
                     const GhosttyTerminalScrollbar *scrollbar, int grid_origin_x,
                     int grid_origin_y, uint16_t term_rows, int pad_right);

#endif
