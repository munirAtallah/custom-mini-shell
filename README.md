# Custom Mini Shell

## Overview
A custom command-line shell written in C. It runs standard programs while adding built-in features: dangerous-command filtering, live timing statistics, pipes, redirection, background jobs, resource-limit control, and a parallel matrix calculator.

## Features
- Dangerous-command filtering from a `danger.txt` list (blocks exact matches, warns on similar commands)
- Live stats prompt showing command count, blocked-command count, and last/average/min/max execution times
- Supports operations:
  - `|` : pipes between two commands
  - `2>` : redirect stderr to a file
  - `&` : run a command in the background
- Built-in `my_tee` (writes stdin to stdout + files, `-a` to append)
- Built-in `rlimit` (show/set CPU, memory, file-size limits via `setrlimit`)
- Built-in `mcalc` — a parallel matrix calculator (see below)
- Proper memory management and error handling

## Usage
```bash
gcc library.c -o myshell -lpthread
./myshell
```
Example commands within the shell:
```bash
echo hello
ls | my_tee out.txt
rlimit show
mcalc "(2,2:1,2,3,4)" "(2,2:5,6,7,8)" "ADD"
done
```

## mcalc — Matrix Calculator
Adds or subtracts two or more square matrices in parallel, using a POSIX threads (`pthreads`) reduction tree.

**Input format:** each matrix is written as `"(N,N:v1,v2,...,vN²)"`
- `N` — the size of the matrix (N rows by N columns)
- `v1,v2,...` — the values, filled in row by row
- Only square matrices are supported, and all matrices must be the same size

**Operations:**
- `"ADD"` — adds all matrices together
- `"SUB"` — subtracts left to right (order matters)

**Command shape:**
```bash
mcalc "matrix1" "matrix2" ... "OPERATION"
```

**Examples:**
```bash
mcalc "(2,2:1,2,3,4)" "(2,2:5,6,7,8)" "ADD"
# → (2,2:6,8,10,12)

mcalc "(2,2:9,8,7,6)" "(2,2:1,2,3,4)" "SUB"
# → (2,2:8,6,4,2)
```
Invalid input (wrong format, non-square, or mismatched sizes) prints `ERR_MAT_INPUT`.

## Example
```
#cmd:0|#dangerous_cmd_blocked:0|last_cmd_time:0.00000|...>>echo hello
hello
#cmd:1|#dangerous_cmd_blocked:0|last_cmd_time:0.00821|...>>mcalc "(2,2:1,2,3,4)" "(2,2:5,6,7,8)" "ADD"
(2,2:6,8,10,12)
#cmd:2|#dangerous_cmd_blocked:0|last_cmd_time:0.00063|...>>mcalc "(2,2:9,8,7,6)" "(2,2:1,2,3,4)" "SUB"
(2,2:8,6,4,2)
#cmd:3|#dangerous_cmd_blocked:0|...>>rm -rf /
ERR: Dangerous command detected ("rm -rf /"). Execution prevented.
#cmd:3|#dangerous_cmd_blocked:1|...>>
```

## Notes
- The blocked-command list is loaded from `danger.txt` at startup.
- Background jobs (`&`) are not timed but still counted.
- `rlimit set` applies limits only to the command that follows it.
