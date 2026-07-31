# p101-error-contract

`p101-error-contract` checks a small but important p101 convention: when a
function uses p101 wrappers, tracing, or p101 error macros, the function should
make its `env` and `err` contracts visible.

The tool consumes the shared `P101FACT` stream produced by
`p101-wrapper-audit --emit-module-facts` and parsed through `lib_c_facts`, so it
does not own a private C parser.

## Usage

```sh
p101-error-contract [-h] [-j] [-q] [-v] [-i <facts.tsv> | -C <compile_commands.json> | -F <p101-wrapper-audit>] [path ...]
```

Examples:

```sh
p101-error-contract
p101-error-contract src include
p101-error-contract -C build-clang/compile_commands.json src
p101-error-contract -F ../p101-wrapper-audit/p101-wrapper-audit src
p101-error-contract -i source-facts.tsv src include
p101-error-contract -j src > error-contract.json
```

If no path is supplied, `src` is scanned.

## Findings

| ID | Meaning |
| --- | --- |
| `P101-ERR-001` | A p101 wrapper call or `P101_TRACE` appears before a visible `p101_env` / `env` contract in the current function. |
| `P101-ERR-002` | A fallible p101 wrapper call or p101 error macro appears before a visible `p101_error` / `err` contract in the current function. |
| `P101-ERR-003` | A fallible p101 wrapper passes `NULL` in the standard error-object position without documenting an intentional best-effort boundary. |
| `P101-ERR-004` | With `-S`, a second fallible p101 call is reachable on the same Clang statement path before the prior error state is checked or returned. |
| `P101-ERR-005` | A source file creates more `p101_error` objects than it destroys. |
| `P101-ERR-006` | A source file creates more `p101_env` objects than it destroys. |

`P101-ERR-004` is intentionally strict. Enable it with `-S` when code must
preserve the first failure and stop before any later fallible side effect. The
default contract accepts the common p101 boundary style where an error object
remains set across a short sequence and is checked or returned at the boundary.

For a deliberately fallible boolean probe where failure *is* the result rather
than an error to report, place
`/* P101_ERROR_CONTRACT_ALLOW_NO_ERROR: reason */` immediately before the
probe. The exception applies only to a call on that line or the immediately
following line, and also admits an intentional `NULL` error argument. It is
visible to reviewers; do not use it to silence ordinary error propagation.

The checker accepts either a signature-level contract discovered from Clang AST
facts:

```c
static int load_file(const struct p101_env *env, struct p101_error *err, const char *path);
```

or a local contract that is created before the first relevant p101 call:

```c
struct p101_error *err = p101_error_create(false);
struct p101_env   *env = p101_env_create(err, NULL);
```

## Admitted inputs

The user gives source/header paths. With `-i`, the tool consumes that exact
P101FACT v2 snapshot and does not invoke Clang again. Otherwise, it runs
`p101-wrapper-audit --emit-module-facts` over those paths and consumes the
resulting TSV fact stream. If the current project has a Clang build named by
`.last-build-dir`, or a `build-clang/compile_commands.json`, that database is
passed automatically so sibling p101 include directories and project defines
are preserved. Use `-C` to select another compile database explicitly. Use `-F`
or `P101_ERROR_CONTRACT_FACT_TOOL` to choose the wrapper-audit executable.
`P101_WRAPPER_AUDIT` is also honored.

## Outputs

Text output is line-oriented:

```text
path/to/file.c:42: P101-ERR-002: fallible p101 call or error macro appears before a visible p101_error/err contract [function_name]
```

With `-j`, the tool emits JSON:

```json
{"schema":"p101-error-contract-findings-v2","findings":[],"summary":{"files_scanned":0,"findings":0}}
```

Each finding uses the common `id`, `severity`, `location`, `message`, and
`evidence` envelope.

## Blind spots

This checker is only as complete as the fact stream it receives. If
`p101-wrapper-audit` cannot parse a translation unit, or the wrong include flags
or compile database are used, the contract report is partial or fails as tool
trouble. The contract judgment is still a teaching heuristic, not a proof of all
possible C control flow.

The flow check follows calls with the standard fallible wrapper contract
(`env, err, ...`), recognizes explicit error guards, and follows conservative
statement-local sequences. A function return propagates its existing `err`
state; the tool does not guess whether a numeric return value means success or
failure. It deliberately does not invent loop back-edges, merge arbitrary
branches, or model switch fallthrough; those require a full compiler CFG and
remain documented blind spots.

Direct libc calls are outside this tool's job; use `p101-wrapper-audit` for
that. Third-party code is only checked if you ask this tool to scan it, and it
may not follow p101 conventions.

The ownership checks are deliberately file-scoped balance checks. They cannot
prove object identity or infer an undocumented ownership transfer through a
return value or output parameter. An intentional transfer should therefore be
made visible in the API and documented when reviewing the finding.

The `needs_env` and `needs_error` decisions come from the resolved callee
signature recorded in P101FACT v2. The discard check relies on the p101 API
convention that fallible wrappers take `env, err` as their first two arguments.
C parsing belongs to
`p101-wrapper-audit`; fact parsing belongs to `lib_c_facts`; this tool owns only
the error-contract policy.

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
