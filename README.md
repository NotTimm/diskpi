# diskpi

Charts of what's eating space in a directory, drawn straight into the
terminal — real antialiased shapes where the terminal supports it, not
blocky ASCII.

```
make
./diskpi                       # current directory, pie chart
./diskpi /var/log              # or any path
./diskpi --chart=doughnut .
./diskpi --chart=list          # show every chart type
```

`make install` puts it in `/usr/local/bin` (`PREFIX=~/.local make install` for
a user install).

## Chart types

Plain `diskpi` always draws a pie chart. Everything else is opt-in via
`--chart=NAME`:

| name       | what it shows |
|------------|----------------|
| `pie`      | circular wedges, one per entry (the default) |
| `doughnut` | pie with the total size and entry count in the hole |
| `bar`      | horizontal bars, largest first |
| `column`   | vertical bars along a baseline |
| `stacked`  | one bar, every entry stacked to 100% |
| `line`     | cumulative share of the total by rank (the Pareto curve) |
| `area`     | the same curve, filled |
| `scatter`  | file count against size, log/log |
| `bubble`   | file count against size, bubble area = size |
| `radar`    | the largest entries as spokes |
| `table`    | comparison table with inline sub-cell bars |
| `gauge`    | how full the filesystem is, and this tree's share of it |

## What it measures

Every entry in the directory is sized — plain files by their own size,
directories by recursively summing their contents the way `du` does. Symlinks
count as the link and are never followed, so a symlink loop can't hang it and
an unreadable directory is skipped rather than aborting the run. The largest
entries get their own slice or bar; everything past what fits on screen
collapses into a single "N smaller items" entry so nothing overflows.

## Rendering

Every chart is defined once as geometry — wedges, rounded bars, polylines,
polygons — on a small antialiased canvas (`canvas.c`), then presented one of
three ways depending on what the terminal can actually do:

- **pixels** — real per-pixel antialiasing, sent as a true-colour image
  through the Kitty graphics protocol (Ghostty, Kitty, WezTerm, Konsole).
- **blocks** — Unicode half blocks (`▀`), giving two colour rows per text row,
  used when the terminal has colour but not graphics.
- **ascii** — one character per cell, used when there is no colour at all
  (piping to a file, `--no-color`, or a genuinely minimal terminal).

Terminal capabilities are detected once at startup by querying the terminal
directly — a Kitty graphics probe, a background-colour query, a cell-size
query — rather than guessing from `$TERM`, and the whole layout (radius,
legend width, how many entries fit) is recomputed from the live terminal size
on every run. `--mode=` and `--cell=WxH` override detection when a terminal
doesn't answer, or answers wrong.
