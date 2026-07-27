# p101-error-contract

`p101-error-contract` checks a small but important p101 convention: when a
function uses p101 wrappers, tracing, or p101 error macros, the function should
make its `env` and `err` contracts visible.

This is a teaching checker. It is deliberately deterministic and conservative
enough to use as a gate, but it is not a full C parser or proof engine.

## Usage

```sh
p101-error-contract [-h] [-j] [-q] [path ...]
```

Examples:

```sh
p101-error-contract
p101-error-contract src include
p101-error-contract -j src > error-contract.json
```

If no path is supplied, `src` is scanned.

## Findings

| ID | Meaning |
| --- | --- |
| `P101-ERR-001` | A p101 wrapper call or `P101_TRACE` appears before a visible `p101_env` / `env` contract in the current function. |
| `P101-ERR-002` | A fallible p101 wrapper call or p101 error macro appears before a visible `p101_error` / `err` contract in the current function. |

The checker accepts either a signature-level contract:

```c
static int load_file(const struct p101_env *env, struct p101_error *err, const char *path);
```

or a local contract that is created before the first relevant p101 call:

```c
struct p101_error *err = p101_error_create(false);
struct p101_env   *env = p101_env_create(err, NULL);
```

## Admitted inputs

The tool scans regular files ending in `.c`, `.h`, `.cc`, `.cpp`, `.hh`, or
`.hpp`. Directories are traversed recursively. Build, coverage, fuzz finding,
fuzz artifact, and `.git` directories are skipped.

## Outputs

Text output is line-oriented:

```text
path/to/file.c:42: P101-ERR-002: fallible p101 call or error macro appears before a visible p101_error/err contract [function_name]
```

With `-j`, the tool emits JSON:

```json
{"schema":"p101-error-contract-v1","findings":[],"summary":{"files_scanned":0,"findings":0}}
```

## Blind spots

This first version uses a simple lexical function scanner. It does not expand
macros, evaluate `#if`, parse comments/strings perfectly, or understand every C
declaration form. It is intended to catch ordinary p101 contract drift in
student code, not prove all possible C programs correct.

Direct libc calls are outside this tool's job; use `p101-wrapper-audit` for
that. Third-party code is only checked if you ask this tool to scan it, and it
may not follow p101 conventions.

Implementation note: directory traversal uses the p101 `opendir` and `closedir`
wrappers, but raw `readdir` for iteration so end-of-directory can be
distinguished cleanly from an error.

## Exit status

| Status | Meaning |
| --- | --- |
| `0` | No findings |
| `1` | Findings were reported |
| `2` | Usage or tool trouble |

## Build and check

Configure a compiler once, then run the gate:

```sh
./change-compiler.sh -c clang
./check.sh --no-fuzz
```

Useful receipts while developing:

```sh
cmake --build build-clang
./test.sh
./build-clang/p101-error-contract src
```
