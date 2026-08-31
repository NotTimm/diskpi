#ifndef CANVAS_H
#define CANVAS_H

/*
 * A small RGBA drawing surface sized to a rectangle of terminal cells.
 *
 * Charts draw shapes in canvas pixel coordinates and never worry about how
 * the result reaches the screen: canvas_present() either ships the buffer to
 * the terminal through the Kitty graphics protocol (one pixel per screen
 * pixel) or paints it with U+2580 half blocks (one pixel per half cell).
 * MODE_ASCII gets no canvas at all - those charts draw text directly.
 *
 * Every primitive is antialiased by supersampling its own coverage, and
 * composites with alpha, so shapes may overlap freely.
 */

#include "diskpi.h"

typedef struct {
    int            w, h;      /* canvas size in pixels */
    int            cols, rows;/* terminal cells it covers */
    double         px_w;      /* pixels per cell, horizontally */
    double         px_h;
    unsigned char *px;        /* RGBA, non-premultiplied, w * h * 4 */
} Canvas;

typedef struct {
    double x, y;
} Pt;

/* Allocate a canvas covering cols x rows cells. Returns NULL if the mode has
 * no pixel surface (MODE_ASCII) or on allocation failure. */
Canvas *canvas_new(const Term *t, Mode mode, int cols, int rows);
void    canvas_free(Canvas *c);
void    canvas_clear(Canvas *c);

/* Called once per output row to print whatever belongs to the right of the
 * chart (a legend line, usually). row is 0-based over total_rows. */
typedef void (*CanvasRowFn)(void *ctx, int row);

/* Print the canvas at the cursor, indented by `indent` columns, calling
 * row_fn after each row's cells. total_rows may exceed c->rows when the
 * legend runs longer than the chart; those extra rows are blank on the left.
 * Leaves the cursor on the line after the last row. */
void canvas_present(Canvas *c, const Term *t, Mode mode, int indent,
                    int total_rows, CanvasRowFn row_fn, void *ctx);

/* ------------------------------------------------------------ primitives */
/* Coordinates are pixels, y down. alpha is 0..1. Angles are degrees measured
 * clockwise from twelve o'clock, which is how the charts think about them. */

void canvas_rect(Canvas *c, double x, double y, double w, double h,
                 RGB col, double alpha);
void canvas_round_rect(Canvas *c, double x, double y, double w, double h,
                       double radius, RGB col, double alpha);
void canvas_circle(Canvas *c, double cx, double cy, double r,
                   RGB col, double alpha);
/* Filled annulus sector: r_inner 0 gives a pie wedge, > 0 a doughnut wedge. */
void canvas_wedge(Canvas *c, double cx, double cy, double r_inner,
                  double r_outer, double a0, double a1, RGB col, double alpha);
void canvas_ring(Canvas *c, double cx, double cy, double r_inner,
                 double r_outer, RGB col, double alpha);
void canvas_line(Canvas *c, double x0, double y0, double x1, double y1,
                 double thickness, RGB col, double alpha);
void canvas_polyline(Canvas *c, const Pt *pts, int n, double thickness,
                     RGB col, double alpha);
void canvas_polygon(Canvas *c, const Pt *pts, int n, RGB col, double alpha);

#endif /* CANVAS_H */
