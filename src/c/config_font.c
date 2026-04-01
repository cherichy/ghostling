#include "config_font.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

/** Bundled Maple Mono NF CN (copied into repo `fonts/`). */
#define GHOSTLING_DEFAULT_FONT_PATH "fonts/MapleMono-NF-CN-Regular.ttf"

static char *trim_ascii(char *s)
{
    char *end;
    while (*s && isspace((unsigned char)*s))
        s++;
    if (*s == 0)
        return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';
    return s;
}

static bool file_is_readable(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

static bool path_is_absolute(const char *path)
{
    if (!path || !path[0])
        return false;
#ifdef _WIN32
    if (isalpha((unsigned char)path[0]) && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/'))
        return true;
    if ((path[0] == '\\' && path[1] == '\\') || path[0] == '\\' ||
        path[0] == '/')
        return true;
    return false;
#else
    return path[0] == '/';
#endif
}

static bool current_working_dir(char *out, size_t out_sz)
{
#ifdef _WIN32
    return _getcwd(out, (int)out_sz) != NULL;
#else
    return getcwd(out, out_sz) != NULL;
#endif
}

static bool try_relative_candidates(const char *base_dir, const char *rel_path,
                                    char *out, size_t out_sz)
{
    static const char *prefixes[] = {
        "",
        "..",
        "../..",
        "../../..",
    };

    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        char candidate[4096];
        int n = 0;
        if (prefixes[i][0])
            n = snprintf(candidate, sizeof(candidate), "%s/%s/%s", base_dir,
                         prefixes[i], rel_path);
        else
            n = snprintf(candidate, sizeof(candidate), "%s/%s", base_dir,
                         rel_path);
        if (n <= 0 || (size_t)n >= sizeof(candidate))
            continue;
        if (!file_is_readable(candidate))
            continue;
        snprintf(out, out_sz, "%s", candidate);
        return true;
    }

    return false;
}

static bool resolve_readable_font_path(const char *path, char *out,
                                       size_t out_sz)
{
    if (!path || !path[0])
        return false;

    if (file_is_readable(path)) {
        snprintf(out, out_sz, "%s", path);
        return true;
    }

    if (path_is_absolute(path))
        return false;

    char cwd[4096];
    if (current_working_dir(cwd, sizeof(cwd)) &&
        try_relative_candidates(cwd, path, out, out_sz))
        return true;

    return false;
}

static bool config_default_path(char *out, size_t out_sz)
{
#ifdef _WIN32
    const char *app = getenv("APPDATA");
    if (!app || !app[0])
        return false;
    int n = snprintf(out, out_sz, "%s\\ghostling\\config", app);
    return n > 0 && (size_t)n < out_sz;
#else
    const char *home = getenv("HOME");
    if (!home || !home[0])
        return false;
    int n = snprintf(out, out_sz, "%s/.config/ghostling/config", home);
    return n > 0 && (size_t)n < out_sz;
#endif
}

static void ascii_lower_copy(char *out, size_t out_sz, const char *in)
{
    if (out_sz == 0)
        return;
    size_t i = 0;
    for (; i + 1 < out_sz && in[i] != '\0'; i++)
        out[i] = (char)tolower((unsigned char)in[i]);
    out[i] = '\0';
}

static bool codepoint_set_valid(const char *set_name)
{
    return strcmp(set_name, "full") == 0 || strcmp(set_name, "compact") == 0 ||
           strcmp(set_name, "latin") == 0;
}

static void config_load_file(const char *path, AppConfig *cfg)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return;

    char line[4608];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim_ascii(line);
        if (*p == 0 || *p == '#')
            continue;

        char *eq = strchr(p, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = trim_ascii(p);
        char *val = trim_ascii(eq + 1);
        if (*key == 0)
            continue;

        if (strcmp(key, "font_path") == 0) {
            if (*val) {
                snprintf(cfg->font_path, sizeof(cfg->font_path), "%s", val);
                cfg->font_path_from_config = true;
            }
        } else if (strcmp(key, "font_codepoint_set") == 0) {
            char lowered[16];
            ascii_lower_copy(lowered, sizeof(lowered), val);
            if (codepoint_set_valid(lowered)) {
                snprintf(cfg->font_codepoint_set,
                         sizeof(cfg->font_codepoint_set), "%s", lowered);
            }
        } else if (strcmp(key, "font_size") == 0) {
            char *end = NULL;
            long n = strtol(val, &end, 10);
            if (end != val && n >= 6 && n <= 256)
                cfg->font_size = (int)n;
        } else if (strcmp(key, "tab_title_font_scale") == 0) {
            char *end = NULL;
            float f = strtof(val, &end);
            if (end != val && f >= 0.2f && f <= 2.0f)
                cfg->tab_title_font_scale = f;
        } else if (strcmp(key, "tab_title_h") == 0) {
            char *end = NULL;
            long n = strtol(val, &end, 10);
            if (end != val && n >= 8 && n <= 128)
                cfg->tab_title_h = (int)n;
        } else if (strcmp(key, "tab_reserved_h") == 0) {
            char *end = NULL;
            long n = strtol(val, &end, 10);
            if (end != val && n >= 8 && n <= 128)
                cfg->tab_reserved_h = (int)n;
        }
    }
    fclose(f);
}

void config_load(AppConfig *cfg)
{
    cfg->font_path[0] = '\0';
    cfg->font_size = 16;
    cfg->font_path_from_config = false;
    snprintf(cfg->font_codepoint_set, sizeof(cfg->font_codepoint_set), "%s",
             "full");
    cfg->tab_title_font_scale = 0.8f;
    cfg->tab_title_h = 18;
    cfg->tab_reserved_h = 24;

    char path[4096];
    if (config_default_path(path, sizeof(path)))
        config_load_file(path, cfg);

    if (!cfg->font_path[0]) {
        snprintf(cfg->font_path, sizeof(cfg->font_path), "%s",
                 GHOSTLING_DEFAULT_FONT_PATH);
    }
}

static bool append_range(int *buf, int *n, int cap, int lo, int hi)
{
    if (lo > hi)
        return true;
    for (int c = lo; c <= hi; c++) {
        if (*n >= cap)
            return false;
        buf[(*n)++] = c;
    }
    return true;
}

int *build_terminal_codepoints(const char *set_name, int *out_count)
{
    const int cap = 65536;
    int *cp = (int *)malloc((size_t)cap * sizeof(int));
    if (!cp)
        return NULL;
    int n = 0;
    const char *set = set_name;
    if (!set || !codepoint_set_valid(set))
        set = "full";

#define R(lo, hi)                                                              \
    do {                                                                       \
        if (!append_range(cp, &n, cap, (lo), (hi))) {                          \
            free(cp);                                                          \
            return NULL;                                                     \
        }                                                                      \
    } while (0)

    /* Base set shared by all profiles: Latin + symbols + box drawing. */
    R(0x20, 0x7E);
    R(0xA0, 0x024F);
    R(0x0300, 0x036F);
    R(0x2000, 0x206F);
    R(0x20A0, 0x20CF);
    R(0x2100, 0x214F);
    R(0x2190, 0x21FF);
    R(0x2200, 0x22FF);
    R(0x2300, 0x23FF);
    R(0x2460, 0x24FF);
    R(0x2500, 0x257F);
    R(0x2580, 0x259F);
    R(0x25A0, 0x25FF);
    R(0x2600, 0x26FF);
    R(0x2700, 0x27BF);
    /* zellij/tmux powerline separators (for example U+E0B0) live in PUA.
     * Keep this narrow to avoid a large atlas jump from loading all PUA codepoints. */
    R(0xE0A0, 0xE0D7);

    if (strcmp(set, "latin") != 0) {
        /* Compact/full keep CJK core blocks for Chinese/Japanese/Korean text. */
        R(0x3000, 0x30FF);
        R(0x31F0, 0x31FF);
        R(0x4E00, 0x9FFF);
        R(0xFF00, 0xFFEF);

        /* Common Nerd Font icon blocks used by prompts/TUI statuslines.
         * Keep these in compact/full so NF glyphs render without forcing
         * the full supplementary-plane icon set. */
        R(0xE000, 0xE00A);
        R(0xE200, 0xE2A9);
        R(0xE300, 0xE3E3);
        R(0xE5FA, 0xE6B8);
        R(0xE700, 0xE8EF);
        R(0xEA60, 0xEC1E);
        R(0xED00, 0xEFCE);
        R(0xF000, 0xF533);
    }

    if (strcmp(set, "full") == 0) {
        /* Full profile keeps all legacy ranges from the previous default. */
        R(0x3200, 0x32FF);
        R(0x3300, 0x33FF);
        R(0x3400, 0x4DBF);
        R(0xFE00, 0xFE0F);
        R(0xFE10, 0xFE1F);
        R(0xFE30, 0xFE4F);

        /* Supplementary-plane Nerd Font glyphs (material/icon extras). */
        R(0xF0001, 0xF1AF0);
    }
#undef R

    *out_count = n;
    return cp;
}

Font load_terminal_font(const char *path, const unsigned char *embed,
                        int embed_size, int font_size_px, int *codepoints,
                        int cp_count)
{
    char resolved_path[4096];
    if (path && path[0] &&
        resolve_readable_font_path(path, resolved_path, sizeof(resolved_path))) {
        Font f = LoadFontEx(resolved_path, font_size_px, codepoints, cp_count);
        if (f.glyphCount > 0 && f.texture.id > 0)
            fprintf(stderr, "ghostling: using font \"%s\"\n", resolved_path);
        if (f.glyphCount > 0 && f.texture.id > 0)
            return f;
        fprintf(stderr,
                "ghostling: LoadFontEx failed for \"%s\", using embedded font\n",
                resolved_path);
    } else if (path && path[0]) {
        fprintf(stderr, "ghostling: font not readable \"%s\", using embedded font\n",
                path);
    }

    fprintf(stderr, "ghostling: using embedded fallback font\n");
    return LoadFontFromMemory(".ttf", embed, embed_size, font_size_px,
                              codepoints, cp_count);
}
