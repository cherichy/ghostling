#ifndef GHOSTLING_CONFIG_FONT_H
#define GHOSTLING_CONFIG_FONT_H

#include <stdbool.h>
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

typedef struct {
    char font_path[4096];
    int font_size;
    bool font_path_from_config;
    /** Codepoint set for font atlas: full | compact | latin. Default full. */
    char font_codepoint_set[16];
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
} AppConfig;

void config_load(AppConfig *cfg);
int *build_terminal_codepoints(const char *set_name, int *out_count);
Font load_terminal_font(const char *path, const unsigned char *embed,
                        int embed_size, int font_size_px, int *codepoints,
                        int cp_count);

#endif
