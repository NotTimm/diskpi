# diskpi

Charts of what's eating space in a directory, drawn straight into the
terminal in plain characters — no images, no graphics protocol, as fast as
the scan itself allows.

```
make
./diskpi                       # current directory, pie chart
./diskpi /var/log              # or any path
./diskpi --chart=radar .
./diskpi --chart=list          # show every chart type
```

`make install` puts it in `/usr/local/bin` (`PREFIX=~/.local make install` for
a user install).

## Chart types

Plain `diskpi` always draws a pie chart. Everything else is opt-in via
`--chart=NAME`:

| name       | what it shows |
|------------|----------------|
| `pie`      | a wedge circle, biggest share first (the default) |
| `doughnut` | pie, with the total and entry count in the hole |
| `bar`      | rounded horizontal bars on a track, largest first |
| `column`   | the same bars standing upright |
| `stacked`  | one row of proportional symbols, every entry to 100% |
| `line`     | cumulative share of the total by rank (the Pareto curve) |
| `area`     | the same curve |
| `scatter`  | file count against size, one line per entry |
| `bubble`   | same table as `scatter` |
| `radar`    | a filled polygon, one spoke per entry |
| `table`    | comparison table with inline sub-cell bars |
| `gauge`    | a 270-degree arc of how full the filesystem is |

## What it measures

Every entry in the directory is sized — plain files by their own size,
directories by recursively summing their contents the way `du` does. Symlinks
count as the link and are never followed, so a symlink loop can't hang it and
an unreadable directory is skipped rather than aborting the run. The largest
entries get their own line; everything past what fits on screen collapses
into a single "N smaller items" entry so nothing overflows.

Sizing every top-level entry is independent of every other one, so once the
directory listing is in hand a pool of threads — one per core — sizes them in
parallel. A directory with a lot of large subtrees (a home directory with
millions of files, say) finishes in roughly (total work) / (core count)
rather than however long one thread alone would take.

## Rendering

Pie/doughnut, bar/column, radar and gauge are drawn as actual shapes using
braille characters (U+2800–U+28FF) — the same trick tools like btop use for
round-looking graphs in plain text. Each terminal cell holds an 8-dot
subgrid (2 wide, 4 tall), so a circle or a rounded bar gets eight times the
resolution of one symbol per cell, with no image protocol, no half-block
colour cells and nothing to decode: it's still just characters and ANSI
colour, printed once (`dots.c`). Stacked/line/area/scatter/bubble/table stay
plain text, since a table's inline bars and a ranked list already read
correctly without a drawn shape.

Terminal size comes straight from the kernel (`ioctl(TIOCGWINSZ)`) and colour
support from `$TERM`/`$COLORTERM`, both instant, so there is no round trip to
the terminal before anything can be drawn. `--no-color` drops colour entirely;
the dot shapes still draw, just uncoloured, and the text-only charts fall
back to one symbol per entry.
