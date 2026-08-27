# Custom Mini Shell

## Overview
A custom command-line shell written in C. It runs standard programs while adding its own built-in features: dangerous-command filtering, live timing statistics, pipes, redirection, background jobs, and resource-limit control.

## Features
- **Dangerous-command filtering** — blocks commands listed in `danger.txt`, and warns when a command is similar to a dangerous one
- **Live stats prompt** — shows command count, blocked-command count, and last/average/min/max execution times
- **Pipes** — `cmd1 | cmd2` using `pipe()` and `dup2()`
- **Redirection** — send stderr to a file with `2>`
- **Background jobs** — run a command in the background with `&`
- **`my_tee`** — a built-in `tee` that writes stdin to stdout and multiple files (supports `-a` to append)
- **`rlimit`** — show or set resource limits (CPU time, memory, file size, open files) via `setrlimit`
- Proper memory management and error handling throughout

## Usage
```bash
gcc library.c -o myshell
./myshell
```
> The shell reads its blocked-command list from a `danger.txt` file in the same directory.

Example session:
```bash
#cmd:0|#dangerous_cmd_blocked:0|last_cmd_time:0.00000|avg_time:0.00000|min_time:0.00000|max_time:0.00000>>echo hello
hello
#cmd:1|#dangerous_cmd_blocked:0|last_cmd_time:0.00042|avg_time:0.00042|min_time:0.00042|max_time:0.00042>>ls | my_tee out.txt
...
#cmd:2|#dangerous_cmd_blocked:0|...>>rlimit show
CPU time: soft=unlimited, hard=unlimited
Memory: soft=-1, hard=-1
...
```

Type `done` or `exit` to quit.

## Notes
- The blocked-command list is loaded from `danger.txt` at startup.
- Background jobs (`&`) are not timed but still counted.
- `rlimit set` applies limits only to the command that follows it.
