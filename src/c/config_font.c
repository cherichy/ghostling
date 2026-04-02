#include "config_font.h"
#include "han_table.h"

#include "raylib.h"

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

static bool config_home_path(char *out, size_t out_sz)
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

static bool config_local_path(char *out, size_t out_sz)
{
    char cwd[4096];
    if (!current_working_dir(cwd, sizeof(cwd)))
        return false;

    int n = snprintf(out, out_sz, "%s/config", cwd);
    if (n > 0 && (size_t)n < out_sz && file_is_readable(out))
        return true;

    n = snprintf(out, out_sz, "%s/config.example", cwd);
    if (n > 0 && (size_t)n < out_sz && file_is_readable(out))
        return true;

    return false;
}

bool config_resolve_path(const char *profile, char *out, size_t out_sz)
{
    (void)profile;

    if (config_local_path(out, out_sz))
        return true;

    if (config_home_path(out, out_sz) && file_is_readable(out))
        return true;

    if (out_sz > 0)
        out[0] = '\0';
    return false;
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
           strcmp(set_name, "latin") == 0 ||
           strcmp(set_name, "han3500") == 0 ||
           strcmp(set_name, "han6500") == 0 ||
           strcmp(set_name, "han8105") == 0;
}

static bool han_table_contains(const uint32_t *table, size_t count, uint32_t cp)
{
    size_t lo = 0;
    size_t hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        uint32_t v = table[mid];
        if (v == cp)
            return true;
        if (v < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    return false;
}

GhostlingHanTier ghostling_han_tier_from_codepoint_set(const char *set_name)
{
    if (!set_name)
        return GHOSTLING_HAN_TIER_NONE;
    if (strcmp(set_name, "han3500") == 0)
        return GHOSTLING_HAN_TIER_3500;
    if (strcmp(set_name, "han6500") == 0)
        return GHOSTLING_HAN_TIER_6500;
    if (strcmp(set_name, "han8105") == 0)
        return GHOSTLING_HAN_TIER_8105;
    return GHOSTLING_HAN_TIER_NONE;
}

GhostlingHanTier ghostling_han_tier_for_codepoint(uint32_t codepoint)
{
    if (codepoint < 0x3400 || codepoint > 0x2CE93)
        return GHOSTLING_HAN_TIER_NONE;

    if (han_table_contains(ghostling_han_level1, ghostling_han_level1_count,
                           codepoint))
        return GHOSTLING_HAN_TIER_3500;
    if (han_table_contains(ghostling_han_level2, ghostling_han_level2_count,
                           codepoint))
        return GHOSTLING_HAN_TIER_6500;
    if (han_table_contains(ghostling_han_level3, ghostling_han_level3_count,
                           codepoint))
        return GHOSTLING_HAN_TIER_8105;
    return GHOSTLING_HAN_TIER_NONE;
}

const char *ghostling_codepoint_set_for_han_tier(GhostlingHanTier tier)
{
    switch (tier) {
    case GHOSTLING_HAN_TIER_3500:
        return "han3500";
    case GHOSTLING_HAN_TIER_6500:
        return "han6500";
    case GHOSTLING_HAN_TIER_8105:
        return "han8105";
    default:
        return NULL;
    }
}

static bool parse_bool_value(const char *val, bool *out)
{
    char lowered[32];
    ascii_lower_copy(lowered, sizeof(lowered), val);
    if (strcmp(lowered, "1") == 0 || strcmp(lowered, "true") == 0 ||
        strcmp(lowered, "yes") == 0 || strcmp(lowered, "on") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(lowered, "0") == 0 || strcmp(lowered, "false") == 0 ||
        strcmp(lowered, "no") == 0 || strcmp(lowered, "off") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_copy_shortcut(const char *val, GhostlingCopyShortcut *out)
{
    char compact[64];
    size_t n = 0;
    for (size_t i = 0; val[i] != '\0' && n + 1 < sizeof(compact); i++) {
        unsigned char c = (unsigned char)val[i];
        if (isspace(c))
            continue;
        compact[n++] = (char)tolower(c);
    }
    compact[n] = '\0';

    if (strcmp(compact, "ctrl+c") == 0 || strcmp(compact, "ctrl-c") == 0 ||
        strcmp(compact, "ctrl_c") == 0) {
        *out = GHOSTLING_COPY_SHORTCUT_CTRL_C;
        return true;
    }

    if (strcmp(compact, "ctrl+shift+c") == 0 ||
        strcmp(compact, "ctrl+shift-c") == 0 ||
        strcmp(compact, "ctrl-shift-c") == 0 ||
        strcmp(compact, "ctrl_shift_c") == 0) {
        *out = GHOSTLING_COPY_SHORTCUT_CTRL_SHIFT_C;
        return true;
    }

    return false;
}

static bool parse_paste_shortcut(const char *val, GhostlingPasteShortcut *out)
{
    char compact[64];
    size_t n = 0;
    for (size_t i = 0; val[i] != '\0' && n + 1 < sizeof(compact); i++) {
        unsigned char c = (unsigned char)val[i];
        if (isspace(c))
            continue;
        compact[n++] = (char)tolower(c);
    }
    compact[n] = '\0';

    if (strcmp(compact, "ctrl+shift+v") == 0 ||
        strcmp(compact, "ctrl+shift-v") == 0 ||
        strcmp(compact, "ctrl-shift-v") == 0 ||
        strcmp(compact, "ctrl_shift_v") == 0) {
        *out = GHOSTLING_PASTE_SHORTCUT_CTRL_SHIFT_V;
        return true;
    }

    if (strcmp(compact, "ctrl+v") == 0 || strcmp(compact, "ctrl-v") == 0 ||
        strcmp(compact, "ctrl_v") == 0) {
        *out = GHOSTLING_PASTE_SHORTCUT_CTRL_V;
        return true;
    }

    if (strcmp(compact, "none") == 0 || strcmp(compact, "disabled") == 0 ||
        strcmp(compact, "off") == 0) {
        *out = GHOSTLING_PASTE_SHORTCUT_NONE;
        return true;
    }

    return false;
}

static bool parse_key_token(const char *token, int *out_key)
{
    if (strcmp(token, "tab") == 0) {
        *out_key = KEY_TAB;
        return true;
    }
    if (strcmp(token, "f5") == 0) {
        *out_key = KEY_F5;
        return true;
    }
    if (strcmp(token, "f6") == 0) {
        *out_key = KEY_F6;
        return true;
    }
    if (strcmp(token, "f7") == 0) {
        *out_key = KEY_F7;
        return true;
    }
    if (strcmp(token, "f8") == 0) {
        *out_key = KEY_F8;
        return true;
    }
    if (strcmp(token, "f9") == 0) {
        *out_key = KEY_F9;
        return true;
    }
    if (strcmp(token, "f10") == 0) {
        *out_key = KEY_F10;
        return true;
    }
    if (strcmp(token, "f11") == 0) {
        *out_key = KEY_F11;
        return true;
    }
    if (strcmp(token, "f12") == 0) {
        *out_key = KEY_F12;
        return true;
    }

    if (strlen(token) == 1) {
        unsigned char c = (unsigned char)token[0];
        if (c >= 'a' && c <= 'z') {
            *out_key = KEY_A + (int)(c - 'a');
            return true;
        }
        if (c >= '0' && c <= '9') {
            *out_key = KEY_ZERO + (int)(c - '0');
            return true;
        }
    }

    return false;
}

static bool parse_profile_key(const char *key, const char *profile,
                              char *out, size_t out_sz)
{
    const char *prefix = "profile.";
    size_t prefix_len = strlen(prefix);
    if (strncmp(key, prefix, prefix_len) != 0)
        return false;

    const char *rest = key + prefix_len;
    const char *dot = strchr(rest, '.');
    if (!dot || dot == rest)
        return false;

    size_t profile_len = (size_t)(dot - rest);
    char key_profile[64];
    if (profile_len >= sizeof(key_profile))
        return false;
    memcpy(key_profile, rest, profile_len);
    key_profile[profile_len] = '\0';

    char lowered_profile[64];
    ascii_lower_copy(lowered_profile, sizeof(lowered_profile), key_profile);

    if (!profile || !profile[0] || strcmp(lowered_profile, profile) != 0)
        return false;

    const char *actual_key = dot + 1;
    if (!actual_key[0])
        return false;

    snprintf(out, out_sz, "%s", actual_key);
    return true;
}

static bool parse_key_binding_value(const char *val, GhostlingKeyBinding *out)
{
    char compact[96];
    size_t n = 0;
    for (size_t i = 0; val[i] != '\0' && n + 1 < sizeof(compact); i++) {
        unsigned char c = (unsigned char)val[i];
        if (isspace(c))
            continue;
        compact[n++] = (char)tolower(c);
    }
    compact[n] = '\0';

    if (compact[0] == '\0' || strcmp(compact, "none") == 0 ||
        strcmp(compact, "off") == 0 || strcmp(compact, "disabled") == 0) {
        out->enabled = false;
        return true;
    }

    uint8_t mods = 0;
    int key = 0;
    bool have_key = false;

    char *cursor = compact;
    while (*cursor) {
        char *plus = strchr(cursor, '+');
        if (plus)
            *plus = '\0';

        if (strcmp(cursor, "shift") == 0)
            mods |= GHOSTLING_KEYMOD_SHIFT;
        else if (strcmp(cursor, "ctrl") == 0 || strcmp(cursor, "control") == 0)
            mods |= GHOSTLING_KEYMOD_CTRL;
        else if (strcmp(cursor, "alt") == 0 || strcmp(cursor, "option") == 0)
            mods |= GHOSTLING_KEYMOD_ALT;
        else if (strcmp(cursor, "cmd") == 0 || strcmp(cursor, "command") == 0 ||
                 strcmp(cursor, "super") == 0 || strcmp(cursor, "meta") == 0)
            mods |= GHOSTLING_KEYMOD_SUPER;
        else if (strcmp(cursor, "primary") == 0)
            mods |= GHOSTLING_KEYMOD_PRIMARY;
        else if (!have_key && parse_key_token(cursor, &key))
            have_key = true;
        else
            return false;

        if (!plus)
            break;
        cursor = plus + 1;
    }

    if (!have_key)
        return false;

    out->key = key;
    out->mods = mods;
    out->enabled = true;
    return true;
}

static void key_binding_set_default(GhostlingKeyBinding *binding, int key,
                                    uint8_t mods)
{
    binding->key = key;
    binding->mods = mods;
    binding->enabled = true;
}

static void config_set_defaults(AppConfig *cfg, const char *profile)
{
    cfg->font_path[0] = '\0';
    cfg->font_size = 16;
    cfg->font_path_from_config = false;
    snprintf(cfg->font_codepoint_set, sizeof(cfg->font_codepoint_set), "%s",
             "full");
    cfg->profile[0] = '\0';
    cfg->loaded_config_path[0] = '\0';
    if (profile && profile[0])
        ascii_lower_copy(cfg->profile, sizeof(cfg->profile), profile);
    cfg->tab_title_font_scale = 1.0f;
    cfg->tab_title_h = 18;
    cfg->tab_reserved_h = 24;
    cfg->selection_copy_on_select = true;
    cfg->selection_copy_shortcut = GHOSTLING_COPY_SHORTCUT_CTRL_C;
#if defined(__APPLE__)
    cfg->paste_shortcut = GHOSTLING_PASTE_SHORTCUT_CTRL_V;
#else
    cfg->paste_shortcut = GHOSTLING_PASTE_SHORTCUT_CTRL_SHIFT_V;
#endif
    key_binding_set_default(&cfg->key_new_tab, KEY_T, GHOSTLING_KEYMOD_PRIMARY);
    key_binding_set_default(&cfg->key_close_tab, KEY_W,
                            GHOSTLING_KEYMOD_PRIMARY);
    key_binding_set_default(&cfg->key_next_tab, KEY_TAB,
                            GHOSTLING_KEYMOD_PRIMARY);
    key_binding_set_default(&cfg->key_prev_tab, KEY_TAB,
                            GHOSTLING_KEYMOD_PRIMARY |
                                GHOSTLING_KEYMOD_SHIFT);
    key_binding_set_default(&cfg->key_toggle_tab_strip, KEY_B,
                            GHOSTLING_KEYMOD_PRIMARY);
    key_binding_set_default(&cfg->key_reload_config, KEY_R,
                            GHOSTLING_KEYMOD_PRIMARY |
                                GHOSTLING_KEYMOD_SHIFT);
}

static bool parse_section_header(const char *line, char *out, size_t out_sz)
{
    size_t len = strlen(line);
    if (len < 3 || line[0] != '[' || line[len - 1] != ']')
        return false;

    char name[128];
    size_t copy_len = len - 2;
    if (copy_len >= sizeof(name))
        copy_len = sizeof(name) - 1;
    memcpy(name, line + 1, copy_len);
    name[copy_len] = '\0';

    char *trimmed = trim_ascii(name);
    if (!trimmed[0])
        return false;

    ascii_lower_copy(out, out_sz, trimmed);
    return true;
}

static void config_scan_profile_from_file(const char *path, char *out,
                                          size_t out_sz)
{
    if (out_sz > 0)
        out[0] = '\0';

    FILE *f = fopen(path, "rb");
    if (!f)
        return;

    char section[64] = "";
    char line[4608];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim_ascii(line);
        if (*p == 0 || *p == '#')
            continue;

        char parsed_section[64];
        if (parse_section_header(p, parsed_section, sizeof(parsed_section))) {
            snprintf(section, sizeof(section), "%s", parsed_section);
            continue;
        }

        if (section[0] != '\0')
            continue;

        char *eq = strchr(p, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = trim_ascii(p);
        char *val = trim_ascii(eq + 1);
        if (strcmp(key, "profile") != 0)
            continue;
        if (!*val)
            continue;

        ascii_lower_copy(out, out_sz, val);
        break;
    }

    fclose(f);
}

static void config_load_file(const char *path, AppConfig *cfg,
                             const char *profile)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return;

    char wanted_profile[64];
    wanted_profile[0] = '\0';
    if (profile && profile[0])
        ascii_lower_copy(wanted_profile, sizeof(wanted_profile), profile);

    char section[64] = "";
    char line[4608];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim_ascii(line);
        if (*p == 0 || *p == '#')
            continue;

        char parsed_section[64];
        if (parse_section_header(p, parsed_section, sizeof(parsed_section))) {
            snprintf(section, sizeof(section), "%s", parsed_section);
            continue;
        }

        bool in_scope = section[0] == '\0';
        if (!in_scope && wanted_profile[0] != '\0' &&
            strcmp(section, wanted_profile) == 0)
            in_scope = true;
        if (!in_scope)
            continue;

        char *eq = strchr(p, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = trim_ascii(p);
        char *val = trim_ascii(eq + 1);
        if (*key == 0)
            continue;

        char resolved_key[96];
        bool prefixed =
            parse_profile_key(key, wanted_profile, resolved_key,
                              sizeof(resolved_key));
        const char *effective_key = prefixed ? resolved_key : key;

        if (strcmp(effective_key, "font_path") == 0) {
            if (*val) {
                snprintf(cfg->font_path, sizeof(cfg->font_path), "%s", val);
                cfg->font_path_from_config = true;
            }
        } else if (strcmp(effective_key, "font_codepoint_set") == 0) {
            char lowered[16];
            ascii_lower_copy(lowered, sizeof(lowered), val);
            if (codepoint_set_valid(lowered)) {
                snprintf(cfg->font_codepoint_set,
                         sizeof(cfg->font_codepoint_set), "%s", lowered);
            }
        } else if (strcmp(effective_key, "font_size") == 0) {
            char *end = NULL;
            long n = strtol(val, &end, 10);
            if (end != val && n >= 6 && n <= 256)
                cfg->font_size = (int)n;
        } else if (strcmp(effective_key, "tab_title_font_scale") == 0) {
            char *end = NULL;
            float f = strtof(val, &end);
            if (end != val && f >= 0.2f && f <= 2.0f)
                cfg->tab_title_font_scale = f;
        } else if (strcmp(effective_key, "tab_title_h") == 0) {
            char *end = NULL;
            long n = strtol(val, &end, 10);
            if (end != val && n >= 8 && n <= 128)
                cfg->tab_title_h = (int)n;
        } else if (strcmp(effective_key, "tab_reserved_h") == 0) {
            char *end = NULL;
            long n = strtol(val, &end, 10);
            if (end != val && n >= 8 && n <= 128)
                cfg->tab_reserved_h = (int)n;
        } else if (strcmp(effective_key, "selection_copy_on_select") == 0) {
            bool b = false;
            if (parse_bool_value(val, &b))
                cfg->selection_copy_on_select = b;
        } else if (strcmp(effective_key, "selection_copy_shortcut") == 0) {
            GhostlingCopyShortcut shortcut = GHOSTLING_COPY_SHORTCUT_CTRL_C;
            if (parse_copy_shortcut(val, &shortcut))
                cfg->selection_copy_shortcut = shortcut;
        } else if (strcmp(effective_key, "paste_shortcut") == 0) {
            GhostlingPasteShortcut shortcut =
                GHOSTLING_PASTE_SHORTCUT_CTRL_SHIFT_V;
            if (parse_paste_shortcut(val, &shortcut))
                cfg->paste_shortcut = shortcut;
        } else if (strcmp(effective_key, "key_new_tab") == 0) {
            GhostlingKeyBinding binding = cfg->key_new_tab;
            if (parse_key_binding_value(val, &binding))
                cfg->key_new_tab = binding;
        } else if (strcmp(effective_key, "key_close_tab") == 0) {
            GhostlingKeyBinding binding = cfg->key_close_tab;
            if (parse_key_binding_value(val, &binding))
                cfg->key_close_tab = binding;
        } else if (strcmp(effective_key, "key_next_tab") == 0) {
            GhostlingKeyBinding binding = cfg->key_next_tab;
            if (parse_key_binding_value(val, &binding))
                cfg->key_next_tab = binding;
        } else if (strcmp(effective_key, "key_prev_tab") == 0) {
            GhostlingKeyBinding binding = cfg->key_prev_tab;
            if (parse_key_binding_value(val, &binding))
                cfg->key_prev_tab = binding;
        } else if (strcmp(effective_key, "key_toggle_tab_strip") == 0) {
            GhostlingKeyBinding binding = cfg->key_toggle_tab_strip;
            if (parse_key_binding_value(val, &binding))
                cfg->key_toggle_tab_strip = binding;
        } else if (strcmp(effective_key, "key_reload_config") == 0) {
            GhostlingKeyBinding binding = cfg->key_reload_config;
            if (parse_key_binding_value(val, &binding))
                cfg->key_reload_config = binding;
        }
    }
    fclose(f);
}

void config_load_profile(AppConfig *cfg, const char *profile)
{
    char selected_profile[64] = "";
    if (profile && profile[0])
        ascii_lower_copy(selected_profile, sizeof(selected_profile), profile);
    else {
        const char *env_profile = getenv("GHOSTLING_PROFILE");
        if (env_profile && env_profile[0])
            ascii_lower_copy(selected_profile, sizeof(selected_profile),
                             env_profile);
    }

    config_set_defaults(cfg, selected_profile);

    char path[4096];
    if (config_resolve_path(selected_profile, path, sizeof(path))) {
        if (selected_profile[0] == '\0') {
            char file_profile[64] = "";
            config_scan_profile_from_file(path, file_profile,
                                          sizeof(file_profile));
            if (file_profile[0]) {
                snprintf(selected_profile, sizeof(selected_profile), "%s",
                         file_profile);
                snprintf(cfg->profile, sizeof(cfg->profile), "%s",
                         file_profile);
            }
        }

        config_load_file(path, cfg, selected_profile[0] ? selected_profile : NULL);
        snprintf(cfg->loaded_config_path, sizeof(cfg->loaded_config_path), "%s",
                 path);
    }

    if (!cfg->font_path[0]) {
        snprintf(cfg->font_path, sizeof(cfg->font_path), "%s",
                 GHOSTLING_DEFAULT_FONT_PATH);
    }
}

void config_load(AppConfig *cfg)
{
    config_load_profile(cfg, NULL);
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

static bool append_codepoints(int *buf, int *n, int cap, const uint32_t *list,
                              size_t list_count)
{
    for (size_t i = 0; i < list_count; i++) {
        if (*n >= cap)
            return false;
        buf[(*n)++] = (int)list[i];
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

    GhostlingHanTier han_tier = ghostling_han_tier_from_codepoint_set(set);

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

    if (han_tier != GHOSTLING_HAN_TIER_NONE) {
        /* Han tiers intentionally avoid Hiragana/Katakana and keep Chinese punctuation/fullwidth. */
        R(0x3000, 0x303F);
        R(0xFF00, 0xFFEF);

        if (!append_codepoints(cp, &n, cap, ghostling_han_level1,
                               ghostling_han_level1_count)) {
            free(cp);
            return NULL;
        }
        if (han_tier >= GHOSTLING_HAN_TIER_6500 &&
            !append_codepoints(cp, &n, cap, ghostling_han_level2,
                               ghostling_han_level2_count)) {
            free(cp);
            return NULL;
        }
        if (han_tier >= GHOSTLING_HAN_TIER_8105 &&
            !append_codepoints(cp, &n, cap, ghostling_han_level3,
                               ghostling_han_level3_count)) {
            free(cp);
            return NULL;
        }
    } else if (strcmp(set, "latin") != 0) {
        /* Compact/full keep CJK core blocks for Chinese/Japanese/Korean text. */
        R(0x3000, 0x30FF);
        R(0x31F0, 0x31FF);
        R(0x4E00, 0x9FFF);
        R(0xFF00, 0xFFEF);
    }

    if (strcmp(set, "latin") != 0) {

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
