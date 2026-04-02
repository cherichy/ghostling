#include "effects.h"
#include <string.h>

enum {
    EFFECT_ICON_OSC_MODE_NORMAL = 0,
    EFFECT_ICON_OSC_MODE_ESC = 1,
    EFFECT_ICON_OSC_MODE_OSC = 2,
};

static void effect_icon_reset_command(EffectsContext *ctx)
{
    ctx->icon_osc_esc_pending = false;
    ctx->icon_osc_cmd_decided = false;
    ctx->icon_osc_collect = true;
    ctx->icon_osc_cmd = 0;
    ctx->icon_osc_cmd_digits = 0;
    ctx->icon_osc_payload_len = 0;
    ctx->icon_osc_payload[0] = '\0';
}

static void effect_icon_apply_payload(EffectsContext *ctx)
{
    if (!ctx->icon_osc_collect || ctx->icon_osc_payload_len == 0)
        return;

    size_t len = ctx->icon_osc_payload_len;
    if (len >= sizeof(ctx->title_icon))
        len = sizeof(ctx->title_icon) - 1;
    memcpy(ctx->title_icon, ctx->icon_osc_payload, len);
    ctx->title_icon[len] = '\0';
}

static void effect_icon_process_byte(EffectsContext *ctx, uint8_t b)
{
    if (!ctx->icon_osc_cmd_decided) {
        if (b >= '0' && b <= '9') {
            if (ctx->icon_osc_cmd <= 99999999u)
                ctx->icon_osc_cmd = ctx->icon_osc_cmd * 10u + (unsigned)(b - '0');
            ctx->icon_osc_cmd_digits++;
            return;
        }

        if (b == ';') {
            ctx->icon_osc_cmd_decided = true;
            /* Ghostty maps OSC command 1 to window title and 2 to icon. In
             * practice many tools still emit OSC 1 as icon/tab metadata, so
             * accept both to keep tab labels useful. */
            ctx->icon_osc_collect = (ctx->icon_osc_cmd_digits > 0 &&
                                     (ctx->icon_osc_cmd == 1u ||
                                      ctx->icon_osc_cmd == 2u));
            return;
        }

        ctx->icon_osc_cmd_decided = true;
        ctx->icon_osc_collect = false;
        return;
    }

    if (!ctx->icon_osc_collect)
        return;

    if (ctx->icon_osc_payload_len + 1 >= sizeof(ctx->icon_osc_payload)) {
        ctx->icon_osc_collect = false;
        ctx->icon_osc_payload_len = 0;
        ctx->icon_osc_payload[0] = '\0';
        return;
    }

    ctx->icon_osc_payload[ctx->icon_osc_payload_len++] = (char)b;
    ctx->icon_osc_payload[ctx->icon_osc_payload_len] = '\0';
}

static void effect_icon_finish_osc(EffectsContext *ctx)
{
    effect_icon_apply_payload(ctx);
    ctx->icon_osc_mode = EFFECT_ICON_OSC_MODE_NORMAL;
    effect_icon_reset_command(ctx);
}

void effect_scan_icon_osc(EffectsContext *ctx, const uint8_t *data, size_t len)
{
    if (!ctx || !data || len == 0)
        return;

    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        switch (ctx->icon_osc_mode) {
        case EFFECT_ICON_OSC_MODE_NORMAL:
            if (b == 0x1B)
                ctx->icon_osc_mode = EFFECT_ICON_OSC_MODE_ESC;
            break;

        case EFFECT_ICON_OSC_MODE_ESC:
            if (b == ']') {
                ctx->icon_osc_mode = EFFECT_ICON_OSC_MODE_OSC;
                effect_icon_reset_command(ctx);
            } else if (b == 0x1B) {
                ctx->icon_osc_mode = EFFECT_ICON_OSC_MODE_ESC;
            } else {
                ctx->icon_osc_mode = EFFECT_ICON_OSC_MODE_NORMAL;
            }
            break;

        case EFFECT_ICON_OSC_MODE_OSC:
            if (ctx->icon_osc_esc_pending) {
                ctx->icon_osc_esc_pending = false;
                if (b == '\\') {
                    effect_icon_finish_osc(ctx);
                } else {
                    effect_icon_process_byte(ctx, 0x1B);
                    effect_icon_process_byte(ctx, b);
                }
                break;
            }

            if (b == 0x07) {
                effect_icon_finish_osc(ctx);
            } else if (b == 0x1B) {
                ctx->icon_osc_esc_pending = true;
            } else {
                effect_icon_process_byte(ctx, b);
            }
            break;

        default:
            ctx->icon_osc_mode = EFFECT_ICON_OSC_MODE_NORMAL;
            break;
        }
    }
}

void effect_write_pty(GhosttyTerminal terminal, void *userdata,
                      const uint8_t *data, size_t len)
{
    (void)terminal;
    EffectsContext *ctx = (EffectsContext *)userdata;
    pty_write(ctx->pty_fd, (const char *)data, len);
}

bool effect_size(GhosttyTerminal terminal, void *userdata,
                 GhosttySizeReportSize *out_size)
{
    (void)terminal;
    EffectsContext *ctx = (EffectsContext *)userdata;
    out_size->rows = ctx->rows;
    out_size->columns = ctx->cols;
    out_size->cell_width = (uint32_t)ctx->cell_width;
    out_size->cell_height = (uint32_t)ctx->cell_height;
    return true;
}

bool effect_device_attributes(GhosttyTerminal terminal, void *userdata,
                              GhosttyDeviceAttributes *out_attrs)
{
    (void)terminal;
    (void)userdata;

    out_attrs->primary.conformance_level = GHOSTTY_DA_CONFORMANCE_VT220;
    out_attrs->primary.features[0] = GHOSTTY_DA_FEATURE_COLUMNS_132;
    out_attrs->primary.features[1] = GHOSTTY_DA_FEATURE_SELECTIVE_ERASE;
    out_attrs->primary.features[2] = GHOSTTY_DA_FEATURE_ANSI_COLOR;
    out_attrs->primary.features[3] = GHOSTTY_DA_FEATURE_CLIPBOARD;
    out_attrs->primary.num_features = 4;

    out_attrs->secondary.device_type = GHOSTTY_DA_DEVICE_TYPE_VT220;
    out_attrs->secondary.firmware_version = 1;
    out_attrs->secondary.rom_cartridge = 0;

    out_attrs->tertiary.unit_id = 0;

    return true;
}

GhosttyString effect_xtversion(GhosttyTerminal terminal, void *userdata)
{
    (void)terminal;
    (void)userdata;
    return (GhosttyString){.ptr = (const uint8_t *)"ghostling", .len = 9};
}

void effect_title_changed(GhosttyTerminal terminal, void *userdata)
{
    EffectsContext *ctx = (EffectsContext *)userdata;
    GhosttyString title = {0};
    if (ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_TITLE, &title) !=
        GHOSTTY_SUCCESS)
        return;

    size_t len = title.len < sizeof(ctx->title_shell) - 1
                     ? title.len
                     : sizeof(ctx->title_shell) - 1;
    memcpy(ctx->title_shell, title.ptr, len);
    ctx->title_shell[len] = '\0';
}

void effect_sync_pwd(GhosttyTerminal terminal, void *userdata)
{
    EffectsContext *ctx = (EffectsContext *)userdata;
    GhosttyString pwd = {0};
    if (ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_PWD, &pwd) !=
        GHOSTTY_SUCCESS)
        return;

    size_t len = pwd.len < sizeof(ctx->pwd) - 1 ? pwd.len : sizeof(ctx->pwd) - 1;
    memcpy(ctx->pwd, pwd.ptr, len);
    ctx->pwd[len] = '\0';
}

bool effect_color_scheme(GhosttyTerminal terminal, void *userdata,
                         GhosttyColorScheme *out_scheme)
{
    (void)terminal;
    (void)userdata;
    (void)out_scheme;
    return false;
}
