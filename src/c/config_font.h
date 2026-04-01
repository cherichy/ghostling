#ifndef GHOSTLING_CONFIG_FONT_H
#define GHOSTLING_CONFIG_FONT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "raylib.h"

typedef enum {
    GHOSTLING_COPY_SHORTCUT_CTRL_C = 0,
    GHOSTLING_COPY_SHORTCUT_CTRL_SHIFT_C = 1,
} GhostlingCopyShortcut;

typedef enum {
    GHOSTLING_PASTE_SHORTCUT_CTRL_SHIFT_V = 0,
    GHOSTLING_PASTE_SHORTCUT_CTRL_V = 1,
    GHOSTLING_PASTE_SHORTCUT_NONE = 2,
} GhostlingPasteShortcut;

typedef enum {
    GHOSTLING_KEYMOD_SHIFT = 1 << 0,
    GHOSTLING_KEYMOD_CTRL = 1 << 1,
    GHOSTLING_KEYMOD_ALT = 1 << 2,
    GHOSTLING_KEYMOD_SUPER = 1 << 3,
    GHOSTLING_KEYMOD_PRIMARY = 1 << 4,
} GhostlingKeyMod;

typedef struct {
    int key;
    uint8_t mods;
    bool enabled;
} GhostlingKeyBinding;

typedef enum {
    GHOSTLING_HAN_TIER_NONE = 0,
    GHOSTLING_HAN_TIER_3500 = 1,
    GHOSTLING_HAN_TIER_6500 = 2,
    GHOSTLING_HAN_TIER_8105 = 3,
} GhostlingHanTier;

typedef struct {
    char font_path[4096];
    int font_size;
    bool font_path_from_config;
    /** Codepoint set for font atlas: full | compact | latin. Default full. */
    char font_codepoint_set[16];
    /** Active config profile name (empty means default profile). */
    char profile[64];
    /** Resolved config path currently loaded (empty when no file exists). */
    char loaded_config_path[4096];
    /** Tab title row uses mono font at (font_size_px * this). Default 0.8. */
    float tab_title_font_scale;
    /** Height in pixels of the top band (title + close). Default 18. */
    int tab_title_h;
    /** Height in pixels of the bottom reserved band per tab. Default 24. */
    int tab_reserved_h;
    /** Copy selected text immediately on mouse release. Default true. */
    bool selection_copy_on_select;
    /** Key chord used to copy a local mouse selection. Default: Ctrl+C (Cmd+C on macOS). */
    GhostlingCopyShortcut selection_copy_shortcut;
    /** Key chord used to paste host clipboard into PTY. Default: Ctrl+Shift+V (Ctrl+V/Cmd+V on macOS). */
    GhostlingPasteShortcut paste_shortcut;
    /** App-level shortcuts (primary = Ctrl on Win/Linux, Command on macOS). */
    GhostlingKeyBinding key_new_tab;
    GhostlingKeyBinding key_close_tab;
    GhostlingKeyBinding key_next_tab;
    GhostlingKeyBinding key_prev_tab;
    GhostlingKeyBinding key_toggle_tab_strip;
    GhostlingKeyBinding key_reload_config;
} AppConfig;

void config_load(AppConfig *cfg);
void config_load_profile(AppConfig *cfg, const char *profile);
bool config_resolve_path(const char *profile, char *out, size_t out_sz);
int *build_terminal_codepoints(const char *set_name, int *out_count);
GhostlingHanTier ghostling_han_tier_from_codepoint_set(const char *set_name);
GhostlingHanTier ghostling_han_tier_for_codepoint(uint32_t codepoint);
const char *ghostling_codepoint_set_for_han_tier(GhostlingHanTier tier);
Font load_terminal_font(const char *path, const unsigned char *embed,
                        int embed_size, int font_size_px, int *codepoints,
                        int cp_count);

#endif
