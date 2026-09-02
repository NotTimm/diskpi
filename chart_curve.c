/*
 * Cumulative share by rank - the Pareto curve, as text.
 *
 * x is entry rank, biggest first; y is the running share of the total, so the
 * height of the curve at rank k is how much of the directory the k largest
 * things hold. Line and area name the same numbers; both print the same
 * table, with the 50% and 80% crossings marked.
 */

#define _DEFAULT_SOURCE

#include "chart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cumulative fractions, clamped and forced monotone: a caller that hands us
 * a zero total leaves NaNs in cum[], and a flat zero curve is a truthful
 * picture of a directory with nothing in it. */
static double *cumulative(const Chart *d)
{
    double *cum = malloc((size_t)d->n * sizeof(double));
    if (!cum)
        return NULL;

    double prev = 0.0;
    for (int i = 0; i < d->n; i++) {
        double v = d->cum[i];
        if (!(v >= 0.0))
            v = prev;
        if (v > 1.0)
            v = 1.0;
        if (v < prev)
            v = prev;
        cum[i] = prev = v;
    }
    return cum;
}

/* Smallest rank whose cumulative share reaches thr, or 0 if none does. */
static int rank_at(const double *cum, int n, double thr)
{
    for (int i = 0; i < n; i++)
        if (cum[i] >= thr)
            return i + 1;
    return 0;
}

void chart_curve(ChartCtx *x, bool filled)
{
    (void)filled;   /* the fill only mattered for the drawing */

    const Chart *d = x ? x->data : NULL;
    if (!x || !d || d->n <= 0 || x->rows < 1 || x->cols < 1)
        return;

    double *cum = cumulative(d);
    int w = x->cols - x->indent;

    if (w < 12)
        w = 12;

    /* The curve's bar is the only picture this chart has, so it grows
     * with the window instead of stopping at a fixed width. */
    int bar_w = w / 3;
    if (bar_w > 48)
        bar_w = 48;
    int mark_w = 8;
    int name_w = w - 3 - 1 - 7 - (bar_w + 3) - mark_w;
    if (name_w < 8) {
        mark_w = 0;
        name_w = w - 3 - 1 - 7 - (bar_w + 3);
    }
    if (name_w < 8) {
        bar_w = 0;
        name_w = w - 11;
    }
    if (name_w < 4)
        name_w = 4;

    int r50 = cum ? rank_at(cum, d->n, 0.5) : 0;
    int r80 = cum ? rank_at(cum, d->n, 0.8) : 0;

    int lines = d->n;
    if (lines > x->rows)
        lines = x->rows;

    for (int i = 0; i < lines; i++) {
        const Entry *e = d->slice[i];
        char raw[600], name[512];
        double share = cum ? cum[i] : 0.0;

        snprintf(raw, sizeof(raw), "%s%s", e->name, e->is_dir ? "/" : "");
        fit(raw, name, sizeof(name), name_w);

        for (int k = 0; k < x->indent; k++)
            fputc(' ', stdout);

        printf("%2d %s", i + 1, name);
        for (int k = display_width(name); k < name_w + 1; k++)
            fputc(' ', stdout);
        printf("%6.1f%%", share * 100.0);

        if (bar_w > 0) {
            int fill = (int)(share * bar_w + 0.5);
            if (fill > bar_w)
                fill = bar_w;
            fputs(" [", stdout);
            for (int k = 0; k < bar_w; k++)
                fputc(k < fill ? '#' : '.', stdout);
            fputc(']', stdout);
        }

        if (mark_w > 0) {
            if (i + 1 == r50)
                fputs(" <- 50%", stdout);
            else if (i + 1 == r80)
                fputs(" <- 80%", stdout);
        }
        fputc('\n', stdout);
    }

    free(cum);
}
