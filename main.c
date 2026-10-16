/*
 * diskpi - a chart of what is using space in a directory.
 */

#define _DEFAULT_SOURCE

#include "chart.h"
#include "diskpi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

/* Symbols stand in for colours when there is no colour to be had. */
static const char symbols[] = "#*+=%@O~xwSVAHZ8&$";
static const int  symbols_len = (int)(sizeof(symbols) - 1);

/* The terminal is the real limit on how many slices are worth drawing; this
 * is only a backstop for a very tall window, past which the thinnest wedges
 * stop being distinguishable however much room there is to list them. */
#define SLICE_CEILING 64

/* An entry smaller than this share of the total is pooled rather than drawn:
 * it cannot be told apart from its neighbours at any terminal size. */
#define MIN_SHARE  0.004

/* ...but always list at least this many, however small they are. */
#define MIN_SLICES 8

/* Narrowest legend line that still fits inside the width it is given:
 * a marker, a six-column name, the size and the percentage. */
#define LEGEND_MIN 26

/* Rows the frame costs: a blank line, the two header lines, a blank line
 * before the chart, and a blank line after it. */
#define FRAME_ROWS 5

static int cmp_entry(const void *a, const void *b)
{
    const Entry *ea = a, *eb = b;
    if (ea->size != eb->size)
        return ea->size < eb->size ? 1 : -1;
    return strcmp(ea->name, eb->name);
}

static void usage(const char *argv0)
{
    printf("usage: %s [options] [path]\n\n", argv0);
    printf("  Charts disk usage for a directory (default: current).\n\n");
    printf("  --chart=NAME  chart type (default pie); "
           "--chart=list names them all\n");
    printf("  --no-color    plain text, one symbol per slice\n");
    printf("  -h, --help    this message\n");
}

static void list_charts(void)
{
    for (int i = 0; i < CHART_COUNT; i++)
        printf("%-9s %s\n", chart_name((ChartKind)i),
               chart_desc((ChartKind)i));
}

int main(int argc, char **argv)
{
    const char *target = ".";
    ChartKind kind = CHART_PIE;
    bool want_color = true;

    /* A drawn chart is tens of thousands of tiny writes - three bytes per
     * braille cell, plus a colour escape per run. Buffering the lot and
     * flushing once at the end turns that into a handful of write calls,
     * and stops a half-drawn chart appearing a row at a time. */
    static char outbuf[1 << 16];
    setvbuf(stdout, outbuf, _IOFBF, sizeof(outbuf));

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "--no-color") == 0 || strcmp(a, "--no-colour") == 0)
            want_color = false;
        else if (strncmp(a, "--chart=", 8) == 0) {
            const char *c = a + 8;
            if (strcmp(c, "list") == 0) {
                list_charts();
                return 0;
            }
            if (!chart_lookup(c, &kind)) {
                fprintf(stderr, "diskpi: unknown chart '%s' "
                                "(try --chart=list)\n", c);
                return 1;
            }
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "diskpi: unknown option '%s'\n", a);
            return 1;
        } else
            target = a;
    }

    int n = 0;
    long long total = 0, total_files = 0;
    Entry *entries = scan_directory(target, &n, &total, &total_files);

    if (!entries && n == 0) {
        /* Distinguish "cannot read" from "nothing in it". */
        if (access(target, R_OK) != 0) {
            fprintf(stderr, "diskpi: %s: %s\n", target, strerror(errno));
            return 1;
        }
    }

    char label[4096];
    if (strcmp(target, ".") == 0) {
        if (!getcwd(label, sizeof(label)))
            snprintf(label, sizeof(label), ".");
    } else
        snprintf(label, sizeof(label), "%s", target);

    Term t;
    term_detect(&t, want_color);

    if (n == 0) {
        printf("diskpi: %s is empty\n", label);
        free(entries);
        return 0;
    }
    if (total == 0) {
        printf("diskpi: %s holds %d entr%s totalling 0 bytes\n",
               label, n, n == 1 ? "y" : "ies");
        for (int i = 0; i < n; i++)
            free(entries[i].name);
        free(entries);
        return 0;
    }

    qsort(entries, (size_t)n, sizeof(Entry), cmp_entry);

    /* ------------------------------------------------------- layout maths */

    int indent = 2;
    int rows_avail = t.rows - FRAME_ROWS;
    if (rows_avail < 1)
        rows_avail = 1;

    /* A wide terminal earns a file-count column in the legend, and a very
     * wide one earns room for longer names rather than a wider margin.
     * Below LEGEND_MIN a legend line cannot hold its fixed size and
     * percentage columns and would overrun. */
    int legend_cap = 54;
    if (t.cols >= 200)      legend_cap = 78;
    else if (t.cols >= 160) legend_cap = 70;
    else if (t.cols >= 130) legend_cap = 62;

    int legend_w = t.cols >= 96 ? legend_cap : t.cols / 2;
    if (legend_w > legend_cap)
        legend_w = legend_cap;
    if (legend_w < LEGEND_MIN)
        legend_w = LEGEND_MIN;
    if (legend_w > t.cols - indent)
        legend_w = t.cols - indent;

    /* How many slices are worth drawing? The legend, not the shape, is the
     * constraint, and every chart lays its own out within these rows. */
    int max_slices = rows_avail;
    if (max_slices > SLICE_CEILING)
        max_slices = SLICE_CEILING;
    if (max_slices < 4)
        max_slices = 4;

    /* --------------------------------------------- group the tail, colour */

    /* Rows are not the only reason to stop listing entries. On a chart that
     * divides one shape between the entries, anything under a fraction of a
     * percent can only be drawn as a sliver, and a run of them turns the
     * chart into confetti and the legend into a column of "0.0%" - those
     * are worth more as one honest "N smaller items" line carrying their
     * combined size. A chart that gives every entry its own row has no such
     * problem and would rather fill the window, so it keeps them all. A
     * floor of eight stops a directory of uniformly tiny things from
     * collapsing to nothing. */
    bool one_shape = (kind == CHART_PIE || kind == CHART_DOUGHNUT ||
                      kind == CHART_STACKED);

    if (one_shape) {
        int visible = n;
        for (int i = 0; i < n; i++)
            if (total > 0 &&
                (double)entries[i].size / (double)total < MIN_SHARE) {
                visible = i;
                break;
            }
        if (visible < MIN_SLICES)
            visible = MIN_SLICES;
        if (max_slices > visible)
            max_slices = visible;
    }
    if (max_slices < 4)
        max_slices = 4;

    int shown = n;
    Entry other;
    char other_name_fallback[64];
    memset(&other, 0, sizeof(other));
    bool has_other = false;
    bool other_name_owned = false;

    if (n > max_slices) {
        shown = max_slices - 1;
        for (int i = shown; i < n; i++) {
            other.size += entries[i].size;
            other.files += entries[i].files;
        }
        int rest = n - shown;
        char buf[64];
        snprintf(buf, sizeof(buf), "%d smaller item%s", rest,
                 rest == 1 ? "" : "s");
        other.name = strdup(buf);
        if (other.name)
            other_name_owned = true;
        else {
            snprintf(other_name_fallback, sizeof(other_name_fallback),
                     "%s", buf);
            other.name = other_name_fallback;
        }
        other.aggregate = true;
        other.color = (RGB){ 128, 128, 138 };
        other.sym = '.';
        has_other = other.size > 0;
        if (!has_other)
            shown = n > max_slices ? max_slices : n;
    }

    int slices = shown + (has_other ? 1 : 0);

    /* Colours are picked knowing how many there will be, so a handful of
     * slices can have the vivid, widely-separated end of the palette to
     * themselves rather than a slice of a formula meant for thirty. */
    for (int i = 0; i < shown; i++) {
        entries[i].color = palette(i, slices);
        entries[i].sym = symbols[i % symbols_len];
    }

    Entry **slice = malloc((size_t)slices * sizeof(Entry *));
    double *cum = malloc((size_t)slices * sizeof(double));
    double *pct = malloc((size_t)slices * sizeof(double));
    if (!slice || !cum || !pct) {
        fprintf(stderr, "diskpi: out of memory\n");
        for (int i = 0; i < n; i++)
            free(entries[i].name);
        free(entries);
        if (other_name_owned)
            free(other.name);
        free(slice);
        free(cum);
        free(pct);
        return 1;
    }

    for (int i = 0; i < shown; i++)
        slice[i] = &entries[i];
    if (has_other)
        slice[shown] = &other;

    double running = 0.0;
    for (int i = 0; i < slices; i++) {
        pct[i] = (double)slice[i]->size / (double)total;
        running += pct[i];
        cum[i] = running;
    }
    cum[slices - 1] = 1.0;

    Chart chart = { slice, cum, pct, slices, 0.0 };

    /* ------------------------------------------------------------- output */

    char total_buf[32], header[4096];
    human_size(total, total_buf, sizeof(total_buf));
    fit(label, header, sizeof(header), t.cols - indent);

    fputc('\n', stdout);
    for (int k = 0; k < indent; k++)
        fputc(' ', stdout);
    t_bold(&t);
    t_fg(&t, entries[0].color);
    fputs(header, stdout);
    t_reset(&t);
    fputc('\n', stdout);

    char raw[256], summary[256];
    snprintf(raw, sizeof(raw), "%s across %d entr%s, %lld file%s", total_buf,
             n, n == 1 ? "y" : "ies", total_files,
             total_files == 1 ? "" : "s");
    fit(raw, summary, sizeof(summary), t.cols - indent);

    for (int k = 0; k < indent; k++)
        fputc(' ', stdout);
    t_dim(&t);
    fputs(summary, stdout);
    t_reset(&t);
    fputc('\n', stdout);
    fputc('\n', stdout);

    ChartCtx ctx = {
        &t, &chart, label, total, total_files, n,
        t.cols, rows_avail, legend_w, indent
    };
    chart_draw(&ctx, kind);

    fputc('\n', stdout);
    fflush(stdout);

    for (int i = 0; i < n; i++)
        free(entries[i].name);
    free(entries);
    if (other_name_owned)
        free(other.name);
    free(slice);
    free(cum);
    free(pct);
    return 0;
}
