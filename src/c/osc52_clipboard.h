#ifndef GHOSTLING_OSC52_CLIPBOARD_H
#define GHOSTLING_OSC52_CLIPBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int mode;
    bool osc_esc_pending;
    bool osc_cmd_decided;
    bool osc_collect;
    unsigned osc_cmd;
    size_t osc_cmd_digits;
    uint8_t *osc_buf;
    size_t osc_len;
    size_t osc_cap;
} Osc52ClipboardState;

void osc52_clipboard_init(Osc52ClipboardState *state);
void osc52_clipboard_deinit(Osc52ClipboardState *state);

/* Scan PTY output for OSC 52 clipboard writes and mirror them to the host
 * clipboard. The input bytes are not modified and should still be passed to
 * libghostty for normal VT processing. */
void osc52_clipboard_scan(Osc52ClipboardState *state, const uint8_t *data,
                          size_t len);

#endif
