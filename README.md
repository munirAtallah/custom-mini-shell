# Mini Shell with Matrix Calculation - mcalc

## Overview

This project extends a custom mini-shell by adding a built-in matrix calculation command: `mcalc`. It allows users to perform parallel matrix addition or subtraction using POSIX threads (`pthreads`).

## Features

- Internal `mcalc` command
- Parses and validates matrix inputs in the form:
  ```
  "(N,N:val1,val2,...,valN^2)"
    N : size of matrix
  ```
- Supports operations:
  - `"ADD"`: Matrix addition
  - `"SUB"`: Matrix subtraction (order-sensitive)
- Uses hierarchical parallel computation (reduction tree)
- Handles input/output through stdin/stdout only
- Proper memory management and error handling

## Usage

```bash
gcc library.c -o mcalc
./mcalc danger.txt
```

Example commands within the shell:
```bash
mcalc "(2,2:1,2,3,4)" "(2,2:5,6,7,8)" "ADD"
mcalc "(3,3:9,8,7,6,5,4,3,2,1)" "(3,3:1,2,3,4,5,6,7,8,9)" "SUB"
done
```

## Notes

- Input format must be strictly respected.
- Output must be in the same matrix format with no extra text.
- Only square matrices are supported.
