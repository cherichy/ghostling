#include "tabs.h"

#include <stdio.h>
#include <string.h>

enum {
    TAB_TOGGLE_W = 22,
    TAB_TOGGLE_H = 28,
};

static float tab_clamp_scale(float scale)
{
    return (scale > 0.0f) ? scale : 1.0f;
}

static float tab_snap_to_physical(float value, float scale)
{
    float s = tab_clamp_scale(scale);
    float scaled = value * s;
    if (scaled >= 0.0f)
        scaled = (float)((int)(scaled + 0.5f));
    else
        scaled = (float)((int)(scaled - 0.5f));
    return scaled / s;
}

static float tab_quantize_font_size(float font_size, float dpi_y)
{
    return tab_snap_to_physical(font_size, dpi_y);
}

static Vector2 tab_current_dpi_scale(void)
{
    Vector2 dpi = GetWindowScaleDPI();
    dpi.x = tab_clamp_scale(dpi.x);
    dpi.y = tab_clamp_scale(dpi.y);
    return dpi;
}

/** Fake bold for tab index digits without a separate bold font face. */
static void draw_text_synthetic_bold(Font font, const char *text, Vector2 pos,
                                     float font_size, Color fg,
                                     Vector2 dpi_scale)
{
    float dx = 1.0f / tab_clamp_scale(dpi_scale.x);
    float dy = 1.0f / tab_clamp_scale(dpi_scale.y);

    Vector2 p = {
        tab_snap_to_physical(pos.x, dpi_scale.x),
        tab_snap_to_physical(pos.y, dpi_scale.y),
    };

    Color embolden = fg;
    embolden.a = (unsigned char)((int)fg.a * 170 / 255);

    DrawTextEx(font, text,
               (Vector2){tab_snap_to_physical(p.x + dx, dpi_scale.x), p.y},
               font_size, 0, embolden);
    DrawTextEx(font, text,
               (Vector2){p.x, tab_snap_to_physical(p.y + dy, dpi_scale.y)},
               font_size, 0, embolden);
    DrawTextEx(font, text,
               (Vector2){tab_snap_to_physical(p.x + dx, dpi_scale.x),
                         tab_snap_to_physical(p.y + dy, dpi_scale.y)},
               font_size, 0, embolden);
    DrawTextEx(font, text, p, font_size, 0, fg);
}

static void truncate_to_width(Font font, float font_size, const char *src,
                              float max_w, char *out, size_t outsz)
{
    if (max_w < 8.0f) {
        out[0] = '\0';
        return;
    }
    if (MeasureTextEx(font, src, font_size, 0).x <= max_w) {
        snprintf(out, outsz, "%s", src);
        return;
    }
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", src);
    size_t n = strlen(tmp);
    while (n > 0) {
        tmp[n - 1] = '\0';
        n--;
        if (MeasureTextEx(font, tmp, font_size, 0).x <= max_w) {
            snprintf(out, outsz, "%s", tmp);
            return;
        }
    }
    if (outsz > 0)
        out[0] = '\0';
}

bool tab_splitter_hit(Vector2 mpos, int strip_w, int scr_h)
{
    (void)scr_h;
    return mpos.x >= (float)strip_w &&
           mpos.x <= (float)(strip_w + TAB_SPLITTER_GRAB);
}

static float tab_icon_font_size(float base_font)
{
    float size = base_font * 1.3f;
    if (size < base_font + 2.0f)
        size = base_font + 2.0f;
    return size;
}

/** Shared geometry for the collapse/expand affordance (splitter, vertical center). */
static void tab_splitter_toggle_bounds(int effective_strip_w, int scr_h, int *tx,
                                       int *ty, int *tw, int *th)
{
    *tw = TAB_TOGGLE_W;
    *th = TAB_TOGGLE_H;
    /* Align right edge to splitter line at x = strip_w - 1. */
    *tx = effective_strip_w - *tw - 1;
    *ty = scr_h / 2 - *th / 2;
}

/** Collapsed: small rect near left edge, vertically centered (matches tab_splitter_toggle_draw). */
static void tab_collapsed_expand_bounds(int scr_h, int *tx, int *ty, int *tw,
                                        int *th)
{
    *tw = TAB_TOGGLE_W;
    *th = TAB_TOGGLE_H;
    *tx = (TAB_COLLAPSED_EDGE_HOVER - *tw) / 2;
    if (*tx < 0)
        *tx = 0;
    *ty = scr_h / 2 - *th / 2;
}

bool tab_splitter_toggle_hit(Vector2 mpos, int effective_strip_w, int scr_h)
{
    /* Collapsed: only the chevron rect — rest of left margin is for terminal / selection. */
    if (effective_strip_w == 0) {
        int tx, ty, tw, th;
        tab_collapsed_expand_bounds(scr_h, &tx, &ty, &tw, &th);
        return mpos.x >= (float)tx && mpos.x < (float)(tx + tw) &&
               mpos.y >= (float)ty && mpos.y < (float)(ty + th);
    }
    /* Expanded: only the chevron rect (center height). Rest of splitter = drag. */
    int tx, ty, tw, th;
    tab_splitter_toggle_bounds(effective_strip_w, scr_h, &tx, &ty, &tw, &th);
    return mpos.x >= (float)tx && mpos.x < (float)(tx + tw) &&
           mpos.y >= (float)ty && mpos.y < (float)(ty + th);
}

bool tab_strip_hit(Vector2 mpos, int strip_w, int scr_h, size_t n_tabs,
                   size_t *idx, TabStripAction *act, int tab_title_h,
                   int tab_reserved_h, bool strip_collapsed)
{
    if (strip_collapsed)
        return false;
    /* Collapse control: do not treat as tab row / new tab. */
    if (tab_splitter_toggle_hit(mpos, strip_w, scr_h))
        return false;
    if (tab_title_h < 1)
        tab_title_h = 1;
    if (tab_reserved_h < 0)
        tab_reserved_h = 0;
    int row_h = tab_title_h + tab_reserved_h;
    if (row_h < 1)
        row_h = 1;

    *act = TAB_STRIP_NONE;
    if (mpos.x < 0.0f || mpos.x >= (float)strip_w)
        return false;

    int new_y0 = scr_h - TAB_NEW_H;
    if (mpos.y >= (float)new_y0) {
        *act = TAB_STRIP_NEW;
        return true;
    }

    int row = (int)(mpos.y / (float)row_h);
    if (row < 0) {
        *act = TAB_STRIP_NONE;
        return true;
    }

    size_t max_vis = (size_t)((new_y0 > 0) ? (new_y0 / row_h) : 1);
    if (max_vis == 0)
        max_vis = 1;

    if ((size_t)row >= n_tabs || (size_t)row >= max_vis) {
        *act = TAB_STRIP_NONE;
        return true;
    }

    *idx = (size_t)row;
    int y_in_tab = (int)mpos.y - row * row_h;
    bool in_title = y_in_tab < tab_title_h;
    if (mpos.x >= (float)(strip_w - TAB_CLOSE_W) && in_title)
        *act = TAB_STRIP_CLOSE;
    else
        *act = TAB_STRIP_SELECT;
    return true;
}

void tab_strip_draw(Font font, float font_size, int strip_w, int scr_h,
                    Tab *const *tabs, size_t n_tabs, size_t active_idx,
                    size_t edit_idx, const char *edit_buf, Color strip_bg,
                    Color tab_index_bg, Color tab_reserved_bg, Color tab_bg,
                    Color tab_active, Color border, Color fg, Color edit_bg,
                    int tab_title_h, int tab_reserved_h, bool strip_collapsed)
{
    if (strip_collapsed)
        return;

    Vector2 dpi_scale = tab_current_dpi_scale();
    font_size = tab_quantize_font_size(font_size, dpi_scale.y);

    if (tab_title_h < 1)
        tab_title_h = 1;
    if (tab_reserved_h < 0)
        tab_reserved_h = 0;
    int row_h = tab_title_h + tab_reserved_h;
    if (row_h < 1)
        row_h = 1;

    int ix = TAB_INDEX_COL_W;
    if (ix >= strip_w - (int)TAB_CLOSE_W - 8)
        ix = (strip_w > 40) ? 20 : 0;

    DrawRectangle(0, 0, strip_w, scr_h, strip_bg);
    DrawRectangle(strip_w - 1, 0, 1, scr_h, border);

    int new_y0 = scr_h - TAB_NEW_H;
    size_t max_vis = (size_t)((new_y0 > 0) ? (new_y0 / row_h) : 1);
    if (max_vis == 0)
        max_vis = 1;

    float label_max_w = (float)(strip_w - ix - TAB_CLOSE_W - 10);
    if (label_max_w < 20.0f)
        label_max_w = 20.0f;

    for (size_t i = 0; i < n_tabs && i < max_vis; i++) {
        int y0 = (int)(i * row_h);
        bool editing = (edit_idx == i);

        DrawRectangle(0, y0, ix, row_h, tab_index_bg);
        DrawRectangle(ix - 1, y0, 1, row_h, border);

        Color title_bg = editing ? edit_bg
                               : ((i == active_idx) ? tab_active : tab_bg);
        DrawRectangle(ix, y0, strip_w - ix - 1, tab_title_h, title_bg);

        int y_res = y0 + tab_title_h;
        DrawRectangle(ix, y_res, strip_w - ix - 1, tab_reserved_h,
                      tab_reserved_bg);

        /* Title / reserved: only to the right of the index column — a full-width
         * line here would cut through the vertically centered index digit. */
        DrawRectangle(ix, y0 + tab_title_h - 1, strip_w - ix - 1, 1, border);
        /* Between two tab rows: full width so the index cells are separated too. */
        DrawRectangle(0, y_res + tab_reserved_h - 1, strip_w - 1, 1, border);

        char num[8];
        snprintf(num, sizeof(num), "%zu", i + 1);
        Vector2 ns = MeasureTextEx(font, num, font_size, 0);
        float nx = ((float)ix - ns.x) * 0.5f;
        if (nx < 2.0f)
            nx = 2.0f;
        float ny = (float)y0 + ((float)row_h - ns.y) * 0.5f;
        Vector2 num_pos = {
            tab_snap_to_physical(nx, dpi_scale.x),
            tab_snap_to_physical(ny, dpi_scale.y),
        };
        if (i == active_idx)
            draw_text_synthetic_bold(font, num, num_pos, font_size, fg,
                                     dpi_scale);
        else
            DrawTextEx(font, num, num_pos, font_size, 0, fg);

        char line[512];
        if (editing && edit_buf)
            snprintf(line, sizeof(line), "%s", edit_buf);
        else {
            char raw[256];
            tab_display_title(tabs[i], i + 1, raw, sizeof(raw));
            truncate_to_width(font, font_size, raw, label_max_w, line,
                              sizeof(line));
        }

        Vector2 ts = MeasureTextEx(font, line, font_size, 0);
        float tx = (float)ix + 6.0f;
        float ty = (float)y0 + ((float)tab_title_h - ts.y) * 0.5f;
        Vector2 title_pos = {
            tab_snap_to_physical(tx, dpi_scale.x),
            tab_snap_to_physical(ty, dpi_scale.y),
        };
        DrawTextEx(font, line, title_pos, font_size, 0, fg);

        if (tab_reserved_h > 0) {
            float status_font = font_size;

            char status_raw[96];
            const char *agent_name =
                ghostling_agent_state_agent(&tabs[i]->agent_state);
            if (agent_name[0] != '\0') {
                snprintf(status_raw, sizeof(status_raw), "[%s]: %s",
                         agent_name,
                         ghostling_agent_state_label(&tabs[i]->agent_state));
            } else {
                snprintf(status_raw, sizeof(status_raw), "%s",
                         ghostling_agent_state_label(&tabs[i]->agent_state));
            }

            char status_line[96];
            truncate_to_width(font, status_font, status_raw, label_max_w,
                              status_line, sizeof(status_line));

            Vector2 ss = MeasureTextEx(font, status_line, status_font, 0);
            float sy = (float)y_res + ((float)tab_reserved_h - ss.y) * 0.5f;
            Vector2 status_pos = {
                tab_snap_to_physical(tx, dpi_scale.x),
                tab_snap_to_physical(sy, dpi_scale.y),
            };
            DrawTextEx(font, status_line, status_pos, status_font, 0, fg);
        }

        const char *x = "×";
        Vector2 xs = MeasureTextEx(font, x, font_size, 0);
        float cx = (float)(strip_w - TAB_CLOSE_W) +
                   (((float)TAB_CLOSE_W - xs.x) * 0.5f);
        Vector2 close_pos = {
            tab_snap_to_physical(cx, dpi_scale.x),
            tab_snap_to_physical(ty, dpi_scale.y),
        };
        DrawTextEx(font, x, close_pos, font_size, 0, fg);
    }

    DrawRectangle(0, new_y0, strip_w - 1, TAB_NEW_H, tab_bg);
    DrawRectangle(0, new_y0, strip_w - 1, 1, border);
    const char *plus = "+";
    float icon_font = tab_icon_font_size(font_size);
    icon_font = tab_quantize_font_size(icon_font, dpi_scale.y);
    Vector2 ps = MeasureTextEx(font, plus, icon_font, 0);
    Vector2 plus_pos = {
        tab_snap_to_physical(((float)strip_w - ps.x) * 0.5f, dpi_scale.x),
        tab_snap_to_physical((float)new_y0 + ((float)TAB_NEW_H - ps.y) * 0.5f,
                             dpi_scale.y),
    };
    draw_text_synthetic_bold(
        font, plus, plus_pos, icon_font, fg, dpi_scale);
}

void tab_splitter_toggle_draw(Font font, float font_size, int strip_w, int scr_h,
                              bool strip_collapsed, bool show, Color fg)
{
    if (!show)
        return;
    Vector2 dpi_scale = tab_current_dpi_scale();
    float icon_font = tab_icon_font_size(font_size);
    icon_font = tab_quantize_font_size(icon_font, dpi_scale.y);
    if (strip_collapsed) {
        int tx, ty, tw, th;
        tab_collapsed_expand_bounds(scr_h, &tx, &ty, &tw, &th);
        const char *ch = ">";
        Vector2 cs = MeasureTextEx(font, ch, icon_font, 0);
        Vector2 pos = {
            tab_snap_to_physical((float)tx + ((float)tw - cs.x) * 0.5f,
                                 dpi_scale.x),
            tab_snap_to_physical((float)ty + ((float)th - cs.y) * 0.5f,
                                 dpi_scale.y),
        };
        DrawTextEx(font, ch, pos, icon_font, 0, fg);
        return;
    }
    int tx, ty, tw, th;
    tab_splitter_toggle_bounds(strip_w, scr_h, &tx, &ty, &tw, &th);
    const char *lt = "<";
    Vector2 ls = MeasureTextEx(font, lt, icon_font, 0);
    Vector2 pos = {
        tab_snap_to_physical((float)tx + ((float)tw - ls.x) * 0.5f,
                             dpi_scale.x),
        tab_snap_to_physical((float)ty + ((float)th - ls.y) * 0.5f,
                             dpi_scale.y),
    };
    DrawTextEx(font, lt, pos, icon_font, 0, fg);
}
