#include "effects.h"
#include <string.h>

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
