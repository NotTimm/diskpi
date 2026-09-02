#ifndef DOTS_H
#define DOTS_H

/*
 * A tiny braille-dot canvas: each terminal cell holds an 8-dot subgrid (2
 * columns by 4 rows, U+2800-U+28FF), the same trick btop and similar tools
 * use for round-looking graphs in plain text. No images, no half-block
 * colour cells - just characters, printed once, with ANSI colour per run.
 *
 * A cell's colour is the average of every dot set inside it. Wedges never
 * overlap, so that average is exact wherever a cell is filled by one wedge
 * and a soft blend on the one cell-row where two wedges meet.
 */

#include "diskpi.h"

typedef struct Dots Dots;

/* NULL if cols/rows is too small to be worth anything. */
Dots *dots_new(int cols, int rows);
void  dots_free(Dots *d);

/* Coordinates, radii and thicknesses below are all in subpixels: 2 per
 * column, 4 per row. Angles are degrees clockwise from twelve o'clock, as
 * the rest of the charts use. */
void dots_wedge(Dots *d, double cx, double cy, double r_in, double r_out,
                double a0, double a1, RGB color);

void dots_rect(Dots *d, double x, double y, double w, double h, RGB color);
void dots_round_rect(Dots *d, double x, double y, double w, double h,
                     double r, RGB color);
void dots_circle(Dots *d, double cx, double cy, double r, RGB color);
void dots_line(Dots *d, double x0, double y0, double x1, double y1,
              double thick, RGB color);

/* pts is a closed polygon of n vertices (not repeating the first). Fill is
 * by the even-odd rule, so a self-intersecting outline is still filled
 * sensibly rather than crashing on it. */
typedef struct { double x, y; } DotPt;
void dots_polygon(Dots *d, const DotPt *pts, int n, RGB color);

/* Prints `rows_used` rows (padded with `indent` spaces first); for rows
 * past the canvas itself, only the indent is printed. row_cb, if given, is
 * called after each row's dots (or its blank line) so the caller can print
 * a legend line beside it - the cursor is left right after the dots. */
typedef void (*DotsRowFn)(void *ctx, int row);
void dots_present(const Dots *d, const Term *t, int indent, int rows_used,
                  DotsRowFn row_cb, void *ctx);

#endif /* DOTS_H */
