#include "tabs.h"

#include <stdio.h>

void tab_display_title(const Tab *t, size_t tab_index_one_based, char *out,
                       size_t outsz)
{
    const EffectsContext *e = &t->effects;
    if (e->title_override[0] != '\0') {
        snprintf(out, outsz, "%s", e->title_override);
        return;
    }
    if (e->title_icon[0] != '\0') {
        snprintf(out, outsz, "%s", e->title_icon);
        return;
    }
    if (e->title_shell[0] != '\0') {
        snprintf(out, outsz, "%s", e->title_shell);
        return;
    }
    (void)tab_index_one_based;
    if (outsz > 0)
        out[0] = '\0';
}
