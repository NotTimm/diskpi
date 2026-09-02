# diskpi

Charts of what's eating space in a directory, printed straight to the
terminal as plain text — as fast as the scan itself allows, nothing else in
the way.

```
make
./diskpi                       # current directory, pie chart
./diskpi /var/log              # or any path
./diskpi --chart=table .
./diskpi --chart=list          # show every chart type
```

`make install` puts it in `/usr/local/bin` (`PREFIX=~/.local make install` for
a user install).

## Chart types

Plain `diskpi` always draws a pie chart. Everything else is opt-in via
`--chart=NAME`:

| name       | what it shows |
|------------|----------------|
| `pie`      | legend of wedges, biggest share first (the default) |
| `doughnut` | pie, with a total-and-count line up top |
| `bar`      | horizontal bars, largest first |
| `column`   | same as `bar` — a column has no meaningful text form |
| `stacked`  | one row of proportional symbols, every entry to 100% |
| `line`     | cumulative share of the total by rank (the Pareto curve) |
| `area`     | the same curve |
| `scatter`  | file count against size, one line per entry |
| `bubble`   | same table as `scatter` |
| `radar`    | the largest entries, named in rank order |
| `table`    | comparison table with inline sub-cell bars |
| `gauge`    | how full the filesystem is, and this tree's share of it |

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

Everything is plain terminal text and ANSI colour — no graphics protocol,
no half-block drawing, nothing that has to be decoded before it's a chart.
Terminal size comes straight from the kernel (`ioctl(TIOCGWINSZ)`) and colour
support from `$TERM`/`$COLORTERM`, both instant, so there is no round trip to
the terminal before anything can be drawn. `--no-color` drops colour entirely
and falls back to one symbol per entry.
