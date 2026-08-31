# diskpi

A terminal pie chart of what's eating space in a directory.

```
make
./diskpi            # current directory
./diskpi /var/log   # or any path
```

`make install` puts it in `/usr/local/bin` (`PREFIX=~/.local make install` for a
user install).

## What it does

Every entry in the directory is measured — plain files by their own size,
directories by recursively summing their contents the way `du` does. Symlinks
are counted as the link, never followed, so symlink loops can't hang it and
unreadable directories are skipped rather than aborting the run.

The ten largest entries get their own slice; everything below that collapses
into a single grey "N smaller items" slice so the legend stays readable.

## Layout

The chart is rasterized per character cell: a cell is filled when it falls
inside the unit circle, and its colour comes from the angle measured clockwise
from twelve o'clock. Horizontal coordinates are scaled 2:1 against vertical
ones, because terminal cells are about twice as tall as they are wide — that's
what keeps the circle round instead of egg-shaped.

The radius is recomputed on every run from the live terminal size
(`TIOCGWINSZ`), bounded by both the rows available and the columns left over
after reserving the legend. Below roughly 40 columns, or when there isn't room
for a circle at least four rows in radius, it falls back to a horizontal bar
list instead.

## Colour

Output uses 256-colour ANSI. Colour is dropped automatically when stdout isn't
a TTY (piping to a file or a pager), or with `--no-color`; in that mode each
slice is drawn with a distinct ASCII symbol shown in the legend, so the chart
still reads in plain text.
