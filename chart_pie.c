/*
 * Pie and doughnut, as text: a legend line per entry, biggest first. There is
 * no drawn circle - the shape a pie chart's numbers describe is exactly what
 * the percentages already say, and a terminal's job here is to say it fast.
 */

#define _DEFAULT_SOURCE

#include "chart.h"

#include <stdio.h>
#include <string.h>

#define LEGEND_MIN 26      /* narrower than this, legend_line overflows */

/* Legend for terminals too narrow for the shared legend line. */
static void compact_line(const ChartCtx *x, int i, int width)
{
    const Entry *e = x->data->slice[i];
    char size_buf[32], raw[600], name_buf[600];

    human_size(e->size, size_buf, sizeof(size_buf));
    int size_w = display_width(size_buf);
    int name_w = width - 2 - size_w - 1;
    if (name_w < 3)
        name_w = 3;

    snprintf(raw, sizeof(raw), "%s%s", e->name, e->is_dir ? "/" : "");
    fit(raw, name_buf, sizeof(name_buf), name_w);

    if (!x->t->color) {
        printf("%c ", e->sym);
    } else {
        t_fg(x->t, e->color);
        fputs("\xe2\x97\x8f ", stdout);          /* U+25CF black circle */
        t_reset(x->t);
    }

    fputs(name_buf, stdout);
    for (int k = display_width(name_buf); k < name_w + 1; k++)
        fputc(' ', stdout);
    fputs(size_buf, stdout);
}

static void more_line(const ChartCtx *x, int more)
{
    t_dim(x->t);
    printf("%d more", more);
    t_reset(x->t);
}

void chart_pie(ChartCtx *x, bool doughnut)
{
    if (!x || !x->t || !x->data || x->data->n <= 0 || x->rows <= 0 ||
        x->cols <= 0)
        return;

    int width = x->cols - x->indent;
    if (width < 8)
        width = 8;
    if (width > 60)
        width = 60;

    int rows = x->rows;
    if (rows <= 0)
        return;

    /* Where the drawing used to put the total in the hole, the doughnut
     * variant leads with a summary line instead. */
    if (doughnut) {
        char size_buf[32];
        human_size(x->total, size_buf, sizeof(size_buf));
        for (int k = 0; k < x->indent; k++)
            fputc(' ', stdout);
        t_bold(x->t);
        printf("%s", size_buf);
        t_reset(x->t);
        printf(" across %d entr%s\n", x->entries,
               x->entries == 1 ? "y" : "ies");
        rows--;
        if (rows <= 0)
            return;
    }

    int n = x->data->n;
    int listed = n, more = 0;
    if (n > rows) {
        listed = rows - 1;
        more = n - listed;
    }

    for (int i = 0; i < listed; i++) {
        for (int k = 0; k < x->indent; k++)
            fputc(' ', stdout);
        if (width >= LEGEND_MIN)
            legend_line(x->t, x->data, i, width, width >= 52);
        else
            compact_line(x, i, width);
        fputc('\n', stdout);
    }

    if (more > 0) {
        for (int k = 0; k < x->indent; k++)
            fputc(' ', stdout);
        more_line(x, more);
        fputc('\n', stdout);
    }
}
