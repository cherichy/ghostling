#ifndef GHOSTLING_CONFIG_FONT_H
#define GHOSTLING_CONFIG_FONT_H

#include <stdbool.h>
#include "raylib.h"

typedef struct {
    char font_path[4096];
    int font_size;
    bool font_path_from_config;
} AppConfig;

void config_load(AppConfig *cfg);
int *build_terminal_codepoints(int *out_count);
Font load_terminal_font(const char *path, const unsigned char *embed,
                        int embed_size, int font_size_px, int *codepoints,
                        int cp_count);

#endif
