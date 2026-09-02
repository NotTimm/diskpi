/*
 * Scatter and bubble, as text: file count against total size, one line per
 * entry. Both variants print the same table - a bubble chart's radius is a
 * transform of the size column that is already there.
 */

#define _DEFAULT_SOURCE

#include "chart.h"

#include <stdio.h>
#include <string.h>

static void spaces(int n)
{
    while (n-- > 0)
        fputc(' ', stdout);
}

void chart_points(ChartCtx *x, bool bubble)
{
    (void)bubble;   /* area vs. radius only mattered for the drawing */

    if (!x || !x->data || x->data->n <= 0 || x->rows <= 0 || x->cols <= 0)
        return;

    const Chart *d = x->data;
    int avail = x->cols - x->indent;
    if (avail < 8)
        avail = 8;

    int budget = x->rows;
    int shown = d->n;
    bool header = budget > shown && avail >= 29;
    if (header)
        budget--;
    if (shown > budget)
        shown = budget;
    if (shown < 0)
        shown = 0;

    int name_w = avail - 21;
    bool wide = name_w >= 8;
    if (!wide)
        name_w = avail - 2;

    if (header) {
        spaces(x->indent);
        t_dim(x->t);
        printf("%-*s %8s  %8s", name_w + 2, "entry", "files", "size");
        t_reset(x->t);
        fputc('\n', stdout);
    }

    for (int i = 0; i < shown; i++) {
        const Entry *e = d->slice[i];
        char raw[600], name[512], size_buf[32];

        snprintf(raw, sizeof(raw), "%s%s", e->name, e->is_dir ? "/" : "");
        fit(raw, name, sizeof(name), name_w);
        human_size(e->size, size_buf, sizeof(size_buf));

        spaces(x->indent);
        if (!x->t->color) {
            printf("%c ", e->sym);
        } else {
            t_fg(x->t, e->color);
            fputs("\xe2\x97\x8f ", stdout);          /* U+25CF */
            t_reset(x->t);
        }

        t_fg(x->t, e->color);
        fputs(name, stdout);
        t_reset(x->t);
        spaces(name_w - display_width(name));

        if (wide) {
            t_dim(x->t);
            printf(" %8lld", e->files);
            t_reset(x->t);
            printf("  %8s", size_buf);
        }
        fputc('\n', stdout);
    }
}
