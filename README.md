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

Entries below a fraction of a percent are pooled into one "N smaller items"
line on the charts that divide a single shape between entries — a pie cannot
draw thirty slivers and a legend of "0.0%" says nothing. Charts that give
each entry its own row keep them all and fill the window instead.

## Speed

Scanning a home directory of ~1.9 million files takes about **0.6 s** on
eight cores. Three things get it there:

- **Parallel at directory granularity.** Every directory found becomes a job
  on a shared stack that any idle worker can take, so one enormous subtree
  spreads across all the cores instead of pinning the single thread that
  happened to draw it. Sizing per *top-level entry* instead was 5× slower on
  the same tree, because one huge subdirectory serialised the whole run.
- **Fewer, cheaper syscalls.** Children are stat'ed relative to their open
  directory (`fstatat`) rather than by full path, so the kernel does not
  re-walk the path for every file, and `readdir`'s `d_type` skips the stat
  entirely for subdirectories. One path string is built per directory rather
  than per file.
- **One flush.** A drawn chart is tens of thousands of three-byte writes;
  the output is buffered and flushed once.

The counts agree exactly with `find`, and repeated runs are byte-identical —
the per-entry totals are atomic and the work queue is the only shared state.

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

Every chart takes the rows and columns the legend does not need, so a big
window gets a big chart rather than a fixed-size one in a sea of margin. Any
pie wedge with room for it is labelled with its share, written over the dots
by rewinding the cursor across the cells just printed.

Colours are picked knowing how many slices there will be: up to twelve get a
hand-picked vivid palette where every neighbouring pair sits a long way round
the wheel, and past that a golden-angle hue walk takes over with a three-step
cycle of lightness and saturation underneath it, so two slices that do land
on a similar hue still differ in weight.

Terminal size comes straight from the kernel (`ioctl(TIOCGWINSZ)`) and colour
support from `$TERM`/`$COLORTERM`, both instant, so there is no round trip to
the terminal before anything can be drawn. `--no-color` drops colour entirely;
the dot shapes still draw, just uncoloured, and the text-only charts fall
back to one symbol per entry.
