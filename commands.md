# Commands

Quick reference for `p101-error-contract`. Every script also supports `--help`.
Run `./change-compiler.sh -c <compiler>` once before building.

| Command | What it does |
| --- | --- |
| `./change-compiler.sh -c <cc>` | Configure the build with a compiler. `--help` lists detected compilers. |
| `./build.sh` | Strict analysis build: format, clang-tidy, cppcheck, static analyzer, `-Werror`, sanitizers. |
| `./check.sh --no-fuzz` | Format + strict build + unit tests. Fuzz is skipped because this tool has no fuzzer yet. |
| `./test.sh` | Build and run the Unity test suite. |
| `./build-clang/p101-error-contract src` | Scan source for missing visible p101 `env` / `err` contracts. |
| `./build-clang/p101-error-contract -j src` | Emit JSON findings and summary. |
| `./clean.sh` | Remove build and generated output. |

Less common: `./build-all.sh` builds with every supported compiler,
`./check-compilers.sh` detects installed compilers, and `./check-env.sh` verifies
required tools.
