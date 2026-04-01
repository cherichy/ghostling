#include "osc52_clipboard.h"

#include "raylib.h"

#include <stdlib.h>
#include <string.h>

enum {
    OSC52_MODE_NORMAL = 0,
    OSC52_MODE_ESC = 1,
    OSC52_MODE_OSC = 2,
};

/* Keep a sane ceiling: clipboard payloads can be large, but we should not
 * allow an unbounded OSC accumulator to grow forever on malformed streams. */
#define OSC52_MAX_COMMAND_BYTES (1024u * 1024u)

static void osc52_reset_command(Osc52ClipboardState *state)
{
    state->osc_esc_pending = false;
    state->osc_cmd_decided = false;
    state->osc_collect = true;
    state->osc_cmd = 0;
    state->osc_cmd_digits = 0;
    state->osc_len = 0;
}

void osc52_clipboard_init(Osc52ClipboardState *state)
{
    memset(state, 0, sizeof(*state));
    state->mode = OSC52_MODE_NORMAL;
    state->osc_collect = true;
}

void osc52_clipboard_deinit(Osc52ClipboardState *state)
{
    free(state->osc_buf);
    memset(state, 0, sizeof(*state));
}

static bool osc52_append_byte(Osc52ClipboardState *state, uint8_t b)
{
    if (state->osc_len >= OSC52_MAX_COMMAND_BYTES)
        return false;
    if (state->osc_len == state->osc_cap) {
        size_t new_cap = state->osc_cap ? state->osc_cap * 2 : 256;
        if (new_cap > OSC52_MAX_COMMAND_BYTES)
            new_cap = OSC52_MAX_COMMAND_BYTES;
        if (new_cap <= state->osc_cap)
            return false;

        uint8_t *new_buf = (uint8_t *)realloc(state->osc_buf, new_cap);
        if (!new_buf)
            return false;
        state->osc_buf = new_buf;
        state->osc_cap = new_cap;
    }

    state->osc_buf[state->osc_len++] = b;
    return true;
}

static int base64_value(uint8_t c)
{
    if (c >= 'A' && c <= 'Z')
        return (int)(c - 'A');
    if (c >= 'a' && c <= 'z')
        return (int)(c - 'a' + 26);
    if (c >= '0' && c <= '9')
        return (int)(c - '0' + 52);
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

static bool base64_decode_text(const uint8_t *in, size_t in_len, char **out,
                               size_t *out_len)
{
    size_t cap = ((in_len / 4) + 1) * 3 + 1;
    char *buf = (char *)malloc(cap);
    if (!buf)
        return false;

    size_t n = 0;
    uint32_t acc = 0;
    int bits = 0;

    for (size_t i = 0; i < in_len; i++) {
        uint8_t c = in[i];
        if (c == '=')
            break;

        int v = base64_value(c);
        if (v < 0) {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                continue;
            free(buf);
            return false;
        }

        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        while (bits >= 8) {
            bits -= 8;
            if (n + 1 >= cap) {
                size_t new_cap = cap * 2;
                char *new_buf = (char *)realloc(buf, new_cap);
                if (!new_buf) {
                    free(buf);
                    return false;
                }
                buf = new_buf;
                cap = new_cap;
            }
            buf[n++] = (char)((acc >> bits) & 0xFFu);
        }
    }

    buf[n] = '\0';
    *out = buf;
    *out_len = n;
    return true;
}

static bool osc52_apply_clipboard(const uint8_t *cmd, size_t len)
{
    if (!cmd || len < 5)
        return false;

    size_t i = 0;
    unsigned command = 0;
    size_t digits = 0;
    while (i < len && cmd[i] >= '0' && cmd[i] <= '9') {
        command = command * 10u + (unsigned)(cmd[i] - '0');
        i++;
        digits++;
    }
    if (digits == 0 || i >= len || cmd[i] != ';' || command != 52u)
        return false;

    i++;
    while (i < len && cmd[i] != ';')
        i++;
    if (i >= len)
        return false;

    i++;
    const uint8_t *payload = cmd + i;
    size_t payload_len = len - i;

    /* OSC 52 query ("?") asks terminal to return clipboard contents. We don't
     * synthesize a response here, so leave it to normal VT handling. */
    if (payload_len == 1 && payload[0] == '?')
        return false;

    if (payload_len == 0) {
        SetClipboardText("");
        return true;
    }

    char *decoded = NULL;
    size_t decoded_len = 0;
    if (!base64_decode_text(payload, payload_len, &decoded, &decoded_len))
        return false;

    (void)decoded_len;
    SetClipboardText(decoded);
    free(decoded);
    return true;
}

static void osc52_process_osc_byte(Osc52ClipboardState *state, uint8_t b)
{
    if ((!state->osc_cmd_decided || state->osc_collect) &&
        !osc52_append_byte(state, b)) {
        /* If buffering fails (OOM or command too large), stop parsing this OSC
         * instance to avoid repeated failed growth attempts on every byte. */
        state->osc_cmd_decided = true;
        state->osc_collect = false;
        state->osc_len = 0;
    }

    if (state->osc_cmd_decided)
        return;

    if (b >= '0' && b <= '9') {
        state->osc_cmd = state->osc_cmd * 10u + (unsigned)(b - '0');
        state->osc_cmd_digits++;
        return;
    }

    if (b == ';') {
        state->osc_cmd_decided = true;
        state->osc_collect =
            (state->osc_cmd_digits > 0 && state->osc_cmd == 52u);
        if (!state->osc_collect)
            state->osc_len = 0;
        return;
    }

    state->osc_cmd_decided = true;
    state->osc_collect = false;
    state->osc_len = 0;
}

static void osc52_finish_osc(Osc52ClipboardState *state)
{
    if (state->osc_collect && state->osc_len > 0)
        (void)osc52_apply_clipboard(state->osc_buf, state->osc_len);

    state->mode = OSC52_MODE_NORMAL;
    osc52_reset_command(state);
}

void osc52_clipboard_scan(Osc52ClipboardState *state, const uint8_t *data,
                          size_t len)
{
    if (!state || !data || len == 0)
        return;

    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        switch (state->mode) {
        case OSC52_MODE_NORMAL:
            if (b == 0x1B)
                state->mode = OSC52_MODE_ESC;
            break;

        case OSC52_MODE_ESC:
            if (b == ']') {
                state->mode = OSC52_MODE_OSC;
                osc52_reset_command(state);
            } else if (b == 0x1B) {
                state->mode = OSC52_MODE_ESC;
            } else {
                state->mode = OSC52_MODE_NORMAL;
            }
            break;

        case OSC52_MODE_OSC:
            if (state->osc_esc_pending) {
                state->osc_esc_pending = false;
                if (b == '\\') {
                    osc52_finish_osc(state);
                } else {
                    osc52_process_osc_byte(state, 0x1B);
                    osc52_process_osc_byte(state, b);
                }
                break;
            }

            if (b == 0x07) {
                osc52_finish_osc(state);
            } else if (b == 0x1B) {
                state->osc_esc_pending = true;
            } else {
                osc52_process_osc_byte(state, b);
            }
            break;

        default:
            state->mode = OSC52_MODE_NORMAL;
            break;
        }
    }
}
