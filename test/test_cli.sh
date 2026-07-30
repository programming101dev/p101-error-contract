#!/usr/bin/env bash
set -euo pipefail

tool=$1
root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd)
facts_tool="$root/p101-wrapper-audit/p101-wrapper-audit"
work=$(mktemp -d "${TMPDIR:-/tmp}/p101-error-contract-test.XXXXXX")
trap 'rm -rf "$work"' EXIT

cp "$(dirname "$0")/contract_fixture.c" "$work/sample.c"
cd "$work"
"$facts_tool" --emit-module-facts "$work/sample.c" >"$work/good.tsv"
cat >"$work/bad-fact-tool" <<'SCRIPT'
#!/usr/bin/env bash
printf 'P101FACT\t2\tFILE\n'
SCRIPT
chmod +x "$work/bad-fact-tool"

expect() {
  wanted=$1
  shift
  set +e
  "$tool" "$@" >"$work/stdout" 2>"$work/stderr"
  got=$?
  set -e
  if [ "$got" -ne "$wanted" ]; then
    cat "$work/stderr" >&2
  fi
  [ "$got" -eq "$wanted" ]
}

expect 0 --help
expect 0 -h
expect 1 -v -F "$facts_tool" "$work/sample.c"
expect 1 -S -F "$facts_tool" "$work/sample.c"
expect 1 -j -v -F "$facts_tool" "$work/sample.c"
expect 1 -q -F "$facts_tool" "$work/sample.c"
expect 2 -i ''
expect 2 -C ''
expect 2 -F ''
expect 2 -i facts -C db
expect 2 -i facts -F tool
expect 2 -Z
expect 2 "-"$'\001'
P101_ERROR_CONTRACT_TEST_OPTION=@ expect 2 -i /dev/null
P101_ERROR_CONTRACT_TEST_OPTION=$'\001' expect 2 -i /dev/null
expect 2 -i
expect 2 -i "$work/missing.tsv"
expect 2 -i /dev/null
printf 'not a fact\n' >"$work/other.tsv"
expect 2 -i "$work/other.tsv"
printf 'P101FACT\t99\tFILE\tx\tx\t0\t0\n' >"$work/bad-version.tsv"
expect 2 -i "$work/bad-version.tsv"
printf 'P101FACT\t2\tFILE\n' >"$work/malformed.tsv"
expect 2 -i "$work/malformed.tsv"
{
  printf '%4095s' '' | tr ' ' x
  printf 'tail\nP101FACT\t2\tFILE\tx\tx\t0\t0\n'
} >"$work/overlong.tsv"
expect 0 -i "$work/overlong.tsv"
expect 2 -F /usr/bin/false "$work/sample.c"
expect 2 -F "$work/bad-fact-tool" "$work/sample.c"
set +e
"$tool" -v -F "$facts_tool" "$work/sample.c" >/dev/null 2>&-
closed_stderr_status=$?
set -e
[ "$closed_stderr_status" -eq 2 ]

many_paths=()
for index in $(seq 1 65); do
  many_paths+=("path-$index")
done
expect 2 "${many_paths[@]}"
expect 2 "${many_paths[@]:0:64}"

for index in $(seq 1 40); do
  P101_FAULT_CALL=$index P101_FAULT_ERRNO=5 \
    "$tool" -i "$work/good.tsv" >/dev/null 2>&1 || :
done
