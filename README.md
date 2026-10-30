# vmd

A small snapshot version control tool written in plain C.

`vmd` copies a project folder into `~/vmd/projects` and gives it a number:
`v1`, `v2`, `v3`, and so on. There is no repository, no server, and no
dependency other than a C compiler and the standard C library.

This is version 2.1.0 of the tool. It keeps the simple style of the first
version, adds an index (`info`, `find`), messages, export, and `.vmdignore`,
and is checked by a small regression suite. It deliberately does **not** use
file locks or `fsync`.

## Build

With make:

```sh
make
```

Or directly:

```sh
cc -std=c11 -O2 -Wall -Wextra *.c -o vmd
```

`./installation.sh` builds the binary and installs it in `~/.local/bin`.

## Tests

```sh
tests/test.sh
```

The script replaces `HOME` with a temporary directory in its first line, so
the real `~/vmd` is never touched. The cleanup trap restores write permission
before removing the sandbox, because several tests create read-only folders.

The suite covers read-only trees and nested read-only trees on `rm`,
`rm <project> all`, `rm all` continuing after a failure, symlink targets
outside the tree, the 0777 snapshot mode, `.vmdignore` diagnostics, copy error
messages, index updates, and export.

## Commands

| Command | What it does |
| :--- | :--- |
| `vmd add <project> [message]` | Save the current folder as `v1`. |
| `vmd up <project> [message]` | Save the current folder as the next version. |
| `vmd imp <project> <version> <dir>` | Export a version to `<dir>/vmd-<project>_v-<version>`. |
| `vmd list` | List projects with their version count and last date. |
| `vmd list <project>` | List the versions of one project. |
| `vmd info` | Show the index path and summary. |
| `vmd info <project> [version]` | Show dates, sizes and messages from the index. |
| `vmd info --rebuild` | Rebuild the index by scanning the saved folders. |
| `vmd find <text>` | Search project names, versions and messages. |
| `vmd rm <project> <version>` | Delete one version. |
| `vmd rm <project> all` | Delete one project and all its versions (asks first). |
| `vmd rm all` | Delete every saved project (asks first). |
| `vmd help` | Show the built-in help. |
| `vmd version` | Show the version of vmd. |

`export` is an alias for `imp`, `log` for `info`, `search` for `find`, and
`remove` for `rm`. `rm -y` and `rm --yes` skip the confirmation question.

Deleting a whole history asks for confirmation. Without a terminal the
question cannot be answered, so `--yes` is required there. `vmd rm <project>`
without a version is refused on purpose: a forgotten argument must not delete
a project.

## Example

```sh
cd ~/work/my-project
vmd add my-project "first working version"
# ... edit files ...
vmd up my-project "add the parser"
vmd list my-project
vmd imp my-project 2 /tmp/export
```

## Storage

```
~/vmd/
├── index.vmd                        one line per saved version
└── projects/
    └── vmd-<project>/
        ├── v1/
        ├── v2/
        └── ...
```

The index line is:

```
project <tab> version <tab> epoch <tab> files <tab> bytes <tab> message
```

`add`, `up` and `rm` keep it up to date. `info` only reads the index, so it is
fast; if the index is lost or the folders were changed by hand, run
`vmd info --rebuild`.

## Ignoring files

Put a `.vmdignore` file in the folder you save. One pattern per line;
`#` starts a comment.

```
.git
*.o
build/
src/generated
```

- a pattern without `/` matches that name at any depth;
- a pattern with `/` is relative to the saved folder;
- a trailing `/` matches directories only.

Ignored entries are not copied and are counted as `skipped`. Export copies a
saved version exactly as it was stored, ignore rules do not apply there.

`!` (negation) is **not supported**. A line beginning with `!` is skipped and
reported with its line number. A line longer than 255 characters is skipped
with a warning too. At most 64 patterns are accepted; a longer list is an
error and the save stops, because silently dropping a rule could copy a file
that was meant to stay out of the snapshot.

## Copy behavior

- Files are copied with `open`/`read`/`write`; no shell command is used.
- Every saved file and folder is stored with mode **0777**. vmd stores the
  work, not the privileges: setuid, setgid and sticky bits are not kept, and
  executable/read-only distinctions in the source are not reproduced.
- Symbolic links are recreated as links. Special files (fifos, sockets,
  devices) are skipped with a warning.
- A snapshot is written into a hidden `.vN.tmp.XXXXXX` folder inside the
  project and renamed to `vN` only when the copy is complete. An interrupted
  copy therefore never appears as a finished version.
- `rm` renames the version (or the whole project) to a hidden name in the
  same folder first, updates the index, then deletes the files. An
  interrupted delete therefore never leaves a half-removed version visible.
  Read-only folders are made writable during deletion, but symbolic links are
  never followed by that `chmod`.
- Copy failures name the operation, the path and the reason (`open`, `read`,
  `write`, `close`), so a full disk is distinguishable from a permission
  problem.
- Two different processes writing the same project at the same time are not
  coordinated: run one `vmd` command at a time.
- Files are not flushed with `fsync`: after a power loss the newest version
  may be incomplete or missing. The tool is meant for local, everyday
  snapshots, not for backups.

## Layout of the source

| File | Purpose |
| :--- | :--- |
| `vmd.c` | command line parsing and dispatch |
| `add.c`, `up.c` | save the current folder as v1 / the next version |
| `imp.c` | export one version |
| `list.c`, `info.c` | listing and index queries (`info`, `find`) |
| `rm.c` | delete a version, a project, or everything |
| `index.c` | read, write and rebuild `~/vmd/index.vmd` |
| `util.c` | names, paths, copying, deleting, formatting |
| `help.c` | the help text |
| `vmd.h` | shared types and function declarations |
| `tests/test.sh` | regression suite with an isolated `HOME` |
