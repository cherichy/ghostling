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
    /** Set by OSC 1 (window icon) parsed from PTY stream. */
    char title_icon[256];
    /** If non-empty, shown in tab bar instead of @ref title_shell (user rename). */
    char title_override[256];
    /** OSC 7 current working directory (UTF-8). */
    char pwd[1024];

    int icon_osc_mode;
    bool icon_osc_esc_pending;
    bool icon_osc_cmd_decided;
    bool icon_osc_collect;
    unsigned icon_osc_cmd;
    size_t icon_osc_cmd_digits;
    char icon_osc_payload[512];
    size_t icon_osc_payload_len;
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
void effect_scan_icon_osc(EffectsContext *ctx, const uint8_t *data, size_t len);

#endif
