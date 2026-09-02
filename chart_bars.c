/*
 * Bars and columns, as text.
 *
 * Both variants print one line per entry: a name, its size and share, and a
 * run of '#' proportional to the largest entry shown, so the shapes compare
 * sizes against each other rather than against the total. A column chart has
 * no meaningful vertical-text form, so it draws the same horizontal bars as
 * the bar chart; the two remain distinct chart names for the flag, not for
 * the output.
 */

#define _DEFAULT_SOURCE

#include "chart.h"

#include <stdio.h>
#include <string.h>

static void spaces(int n)
{
    for (int i = 0; i < n; i++)
        fputc(' ', stdout);
}

static long long largest(const Chart *d, int n)
{
    long long max = 0;
    for (int i = 0; i < n; i++)
        if (d->slice[i]->size > max)
            max = d->slice[i]->size;
    return max;
}

static void print_note(const Term *t, int hidden, int width)
{
    char raw[64], buf[64];

    snprintf(raw, sizeof(raw), "+%d more not shown", hidden);
    fit(raw, buf, sizeof(buf), width);
    t_dim(t);
    fputs(buf, stdout);
    t_reset(t);
}

/* One line per entry, hash marks for the bar. */
static void plain_bars(ChartCtx *x)
{
    const Chart *d = x->data;
    int width = x->cols - x->indent;
    if (width < 8)
        width = 8;

    int shown = d->n;
    bool note = false;
    if (shown > x->rows) {
        shown = x->rows > 1 ? x->rows - 1 : 1;
        note = shown < d->n && shown + 1 <= x->rows;
    }
    if (shown < 1)
        shown = 1;

    /* Under 26 columns nothing lines up, so the line collapses to a name
     * and a size trimmed together to whatever there is. */
    bool compact = width < 26;

    int name_w = width / 3;
    if (name_w > 24) name_w = 24;
    if (name_w < 8)  name_w = 8;

    int bar_w = width - (name_w + 19);
    if (bar_w < 4)
        bar_w = 0;

    long long max = largest(d, shown);

    for (int i = 0; i < shown; i++) {
        const Entry *e = d->slice[i];
        char raw[600], name[600], size_buf[32];

        human_size(e->size, size_buf, sizeof(size_buf));

        if (compact) {
            char line[700];
            snprintf(raw, sizeof(raw), "%s%s", e->name,
                     e->is_dir ? "/" : "");
            snprintf(line, sizeof(line), "%s  %s", raw, size_buf);
            fit(line, name, sizeof(name), width);
            spaces(x->indent);
            fputs(name, stdout);
            fputc('\n', stdout);
            continue;
        }

        snprintf(raw, sizeof(raw), "%s%s", e->name, e->is_dir ? "/" : "");
        fit(raw, name, sizeof(name), name_w);

        spaces(x->indent);
        printf("%c ", e->sym ? e->sym : '#');
        fputs(name, stdout);
        spaces(name_w - display_width(name) + 1);
        printf("%8s %5.1f%%", size_buf, d->pct[i] * 100.0);

        if (bar_w > 0) {
            int fill = 0;
            if (max > 0)
                fill = (int)((double)bar_w * (double)e->size /
                             (double)max + 0.5);
            if (fill == 0 && e->size > 0)
                fill = 1;
            if (fill > bar_w)
                fill = bar_w;

            fputc(' ', stdout);
            for (int k = 0; k < fill; k++)
                fputc('#', stdout);
        }
        fputc('\n', stdout);
    }

    if (note) {
        spaces(x->indent);
        print_note(x->t, d->n - shown, width);
        fputc('\n', stdout);
    }
}

void chart_bars(ChartCtx *x, bool vertical)
{
    (void)vertical;   /* both variants print the same lines */

    if (!x || !x->data || x->data->n <= 0 || x->rows < 1 || x->cols < 1)
        return;

    plain_bars(x);
}
