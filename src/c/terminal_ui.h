#ifndef GHOSTLING_TERMINAL_UI_H
#define GHOSTLING_TERMINAL_UI_H

#include "pty_common.h"
#include <ghostty/vt.h>
#include "raylib.h"

void log_build_info(void);
void handle_mouse(PtyHandle pty_fd, GhosttyMouseEncoder encoder,
                  GhosttyMouseEvent event, GhosttyTerminal terminal,
                  int cell_width, int cell_height, int pad);
void handle_input(PtyHandle pty_fd, GhosttyKeyEncoder encoder,
                  GhosttyKeyEvent event, GhosttyTerminal terminal);
bool handle_scrollbar(GhosttyTerminal terminal, GhosttyRenderState render_state,
                      bool *dragging);
void render_terminal(GhosttyRenderState render_state,
                     GhosttyRenderStateRowIterator row_iter,
                     GhosttyRenderStateRowCells cells, Font font,
                     int cell_width, int cell_height, int font_size,
                     const GhosttyTerminalScrollbar *scrollbar);

#endif
