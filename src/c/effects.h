#ifndef GHOSTLING_EFFECTS_H
#define GHOSTLING_EFFECTS_H

#include "pty_common.h"
#include <ghostty/vt.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    PtyHandle pty_fd;
    int cell_width;
    int cell_height;
    uint16_t cols;
    uint16_t rows;
    /** Set by OSC title sequences from the shell (ghostty callback). */
    char title_shell[256];
    /** If non-empty, shown in tab bar instead of @ref title_shell (user rename). */
    char title_override[256];
    /** OSC 7 current working directory (UTF-8). */
    char pwd[1024];
} EffectsContext;

void effect_write_pty(GhosttyTerminal terminal, void *userdata,
                      const uint8_t *data, size_t len);
bool effect_size(GhosttyTerminal terminal, void *userdata,
                 GhosttySizeReportSize *out_size);
bool effect_device_attributes(GhosttyTerminal terminal, void *userdata,
                              GhosttyDeviceAttributes *out_attrs);
GhosttyString effect_xtversion(GhosttyTerminal terminal, void *userdata);
void effect_title_changed(GhosttyTerminal terminal, void *userdata);
void effect_sync_pwd(GhosttyTerminal terminal, void *userdata);
bool effect_color_scheme(GhosttyTerminal terminal, void *userdata,
                         GhosttyColorScheme *out_scheme);

#endif
