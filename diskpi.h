#ifndef DISKPI_H
#define DISKPI_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>

typedef struct {
    unsigned char r, g, b;
} RGB;

typedef struct {
    char      *name;
    long long  size;
    long long  files;
    bool       is_dir;
    bool       aggregate;   /* the pooled "N smaller items" slice */
    RGB        color;
    char       sym;      /* stand-in for colour when colour is unavailable */
} Entry;

typedef enum {
    MODE_AUTO,
    MODE_PIXELS,    /* kitty graphics protocol: real, antialiased pixels */
    MODE_BLOCKS,    /* U+2580 half blocks: two colour rows per text row */
    MODE_ASCII      /* one character per cell, no colour needed */
} Mode;

typedef struct {
    int  cols, rows;      /* terminal size in cells */
    int  cell_w, cell_h;  /* size of one cell in pixels, 0 when unknown */
    bool color;
    bool truecolor;
    bool kitty;           /* graphics protocol answered our query */
    bool have_bg;
    RGB  bg;              /* terminal background, for antialiasing */
} Term;

/* One drawable wedge, plus where it sits on the circle. */
typedef struct {
    Entry  **slice;
    double  *cum;         /* cumulative fraction, ending at 1.0 */
    double  *pct;
    int      n;
    double   gap;         /* separator between wedges, in radius units */
} Chart;

/* term.c */
void term_detect(Term *t, bool want_color);
void t_fg(const Term *t, RGB c);
void t_bg(const Term *t, RGB c);
void t_bg_default(const Term *t);
void t_reset(const Term *t);
void t_bold(const Term *t);
void t_dim(const Term *t);

/* scan.c */
Entry *scan_directory(const char *path, int *count,
                      long long *total, long long *files);

/* render.c */
RGB  hsl(double h, double s, double l);
void human_size(long long bytes, char *out, size_t out_len);
int  display_width(const char *s);
void fit(const char *src, char *dst, size_t dst_len, int width);

#endif /* DISKPI_H */
