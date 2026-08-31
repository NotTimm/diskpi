/*
 * diskpi - a pie chart of what is using space in a directory.
 */

#define _DEFAULT_SOURCE

#include "diskpi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

/* Symbols stand in for colours when there is no colour to be had. */
static const char symbols[] = "#*+=%@O~xwSVAHZ8&$";
static const int  symbols_len = (int)(sizeof(symbols) - 1);

/* Slices above this many entries are pointless: the wedges get too thin to
 * see and the legend outgrows the screen. The real limit is the terminal. */
#define SLICE_CEILING 28

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
    printf("  Draws a pie chart of disk usage for a directory "
           "(default: current).\n\n");
    printf("  --mode=MODE   pixels | blocks | ascii | auto (default auto)\n");
    printf("                pixels uses the terminal graphics protocol,\n");
    printf("                blocks uses half-block characters.\n");
    printf("  --cell=WxH    cell size in pixels, when the terminal will\n");
    printf("                not report its own\n");
    printf("  --no-color    plain text, one symbol per slice\n");
    printf("  -h, --help    this message\n");
}

int main(int argc, char **argv)
{
    const char *target = ".";
    Mode wanted = MODE_AUTO;
    bool want_color = true;
    int force_cell_w = 0, force_cell_h = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "--no-color") == 0 || strcmp(a, "--no-colour") == 0)
            want_color = false;
        else if (strncmp(a, "--mode=", 7) == 0) {
            const char *m = a + 7;
            if (strcmp(m, "pixels") == 0)      wanted = MODE_PIXELS;
            else if (strcmp(m, "blocks") == 0) wanted = MODE_BLOCKS;
            else if (strcmp(m, "ascii") == 0)  wanted = MODE_ASCII;
            else if (strcmp(m, "auto") == 0)   wanted = MODE_AUTO;
            else {
                fprintf(stderr, "diskpi: unknown mode '%s'\n", m);
                return 1;
            }
        } else if (strncmp(a, "--cell=", 7) == 0) {
            if (sscanf(a + 7, "%dx%d", &force_cell_w, &force_cell_h) != 2 ||
                force_cell_w <= 0 || force_cell_h <= 0) {
                fprintf(stderr, "diskpi: --cell wants WxH in pixels, "
                                "e.g. --cell=9x18\n");
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

    if (force_cell_w > 0) {
        t.cell_w = force_cell_w;
        t.cell_h = force_cell_h;
        if (wanted == MODE_PIXELS)
            t.kitty = true;     /* the user is asserting support */
    }

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
    int rows_avail = t.rows - 4;
    if (rows_avail < 4)
        rows_avail = 4;

    /* A wide terminal earns a file-count column in the legend. */
    int legend_w = t.cols >= 96 ? 54 : t.cols / 2;
    if (legend_w > 54)
        legend_w = 54;
    if (legend_w < 24)
        legend_w = 24;

    int by_cols = (t.cols - indent - legend_w - 3) / 4;
    int by_rows = rows_avail / 2;
    int radius = by_cols < by_rows ? by_cols : by_rows;
    if (radius > 26)
        radius = 26;

    bool pie = radius >= 4 && t.cols >= 44;

    /* How many wedges fit? The legend, not the circle, is the constraint. */
    int max_slices = pie ? rows_avail : rows_avail / 2;
    if (max_slices > SLICE_CEILING)
        max_slices = SLICE_CEILING;
    if (max_slices < 4)
        max_slices = 4;

    /* --------------------------------------------- group the tail, colour */

    int shown = n;
    Entry other;
    memset(&other, 0, sizeof(other));
    bool has_other = false;

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
        other.aggregate = true;
        other.color = (RGB){ 128, 128, 138 };
        other.sym = '.';
        has_other = other.size > 0;
        if (!has_other)
            shown = n > max_slices ? max_slices : n;
    }

    int slices = shown + (has_other ? 1 : 0);

    /* Hues walk by the golden angle so neighbouring wedges never land on
     * neighbouring colours, however many slices there turn out to be. */
    for (int i = 0; i < shown; i++) {
        double hue = 205.0 + 137.508 * i;
        double light = (i % 2) ? 0.56 : 0.65;
        double sat = (i % 3 == 2) ? 0.60 : 0.72;
        entries[i].color = hsl(hue, sat, light);
        entries[i].sym = symbols[i % symbols_len];
    }

    Entry **slice = malloc((size_t)slices * sizeof(Entry *));
    double *cum = malloc((size_t)slices * sizeof(double));
    double *pct = malloc((size_t)slices * sizeof(double));
    if (!slice || !cum || !pct) {
        fprintf(stderr, "diskpi: out of memory\n");
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

    /* ------------------------------------------------------ pick a driver */

    Mode mode;
    if (wanted != MODE_AUTO)
        mode = wanted;
    else if (t.kitty && t.color)
        mode = MODE_PIXELS;
    else if (t.color)
        mode = MODE_BLOCKS;
    else
        mode = MODE_ASCII;

    if (mode == MODE_PIXELS && (!t.kitty || t.cell_w <= 0))
        mode = t.color ? MODE_BLOCKS : MODE_ASCII;
    if (mode != MODE_ASCII && !t.color)
        mode = MODE_ASCII;

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

    for (int k = 0; k < indent; k++)
        fputc(' ', stdout);
    t_dim(&t);
    printf("%s across %d entr%s, %lld file%s\n", total_buf, n,
           n == 1 ? "y" : "ies", total_files, total_files == 1 ? "" : "s");
    t_reset(&t);
    fputc('\n', stdout);

    if (pie)
        render_pie(&t, &chart, mode, radius, legend_w, indent);
    else
        render_bars(&t, &chart, t.cols);

    fputc('\n', stdout);
    fflush(stdout);

    for (int i = 0; i < n; i++)
        free(entries[i].name);
    free(entries);
    free(other.name);
    free(slice);
    free(cum);
    free(pct);
    return 0;
}
