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
expect 1 -S "$work/sample.c"
grep -q 'P101-ERR-004' "$work/stdout"
expect 1 -v "$work/sample.c"
grep -q 'P101-ERR-005' "$work/stdout"
grep -q 'P101-ERR-006' "$work/stdout"
grep -q 'P101-ERR-007' "$work/stdout"
grep -q 'P101-ERR-008' "$work/stdout"
grep -q 'P101-ERR-009' "$work/stdout"
[ "$(grep -c 'P101-ERR-007' "$work/stdout")" -eq 1 ]
grep -q '\[arbitrary_termination\]' "$work/stdout"
expect 1 -S "$work/sample.c"
for diagnostic_id in \
  P101-ERR-001 \
  P101-ERR-002 \
  P101-ERR-003 \
  P101-ERR-004 \
  P101-ERR-005 \
  P101-ERR-006 \
  P101-ERR-007 \
  P101-ERR-008 \
  P101-ERR-009
do
  grep -q "$diagnostic_id" "$work/stdout"
done
expect 1 -j -v "$work/sample.c"
expect 1 -q "$work/sample.c"
cat >"$work/balanced.tsv" <<'FACTS'
P101FACT	6	CALL	balanced.c	balanced	0	1	p101_error_create	0	0	0	balanced	c:@F@p101_error_create	c:@F@balanced	0	0
P101FACT	6	CALL	balanced.c	balanced	0	2	p101_error_destroy	0	0	0	balanced	c:@F@p101_error_destroy	c:@F@balanced	0	0
P101FACT	6	CALL	balanced.c	balanced	0	3	p101_env_create	0	0	0	balanced	c:@F@p101_env_create	c:@F@balanced	0	0
P101FACT	6	CALL	balanced.c	balanced	0	4	p101_env_destroy	0	0	0	balanced	c:@F@p101_env_destroy	c:@F@balanced	0	0
FACTS
expect 0 -i "$work/balanced.tsv"
cat >"$work/termination.tsv" <<'FACTS'
P101FACT	6	FUNCTION	termination.c	termination	0	1	helper	1	0	c:@F@helper	0	0
P101FACT	6	CALL	termination.c	termination	0	3	exit	0	0	0	helper	c:@F@exit	c:@F@helper	0	0
FACTS
expect 1 -i "$work/termination.tsv"
grep -q 'P101-ERR-007' "$work/stdout"
cat >"$work/multiple-exits.tsv" <<'FACTS'
P101FACT	6	FUNCTION	multiple.c	multiple	0	1	helper	1	0	c:@F@helper	0	0
P101FACT	6	NOTE	multiple.c	multiple	0	3	FUNCTION_RETURN	helper	5	c:@F@helper	0	0
P101FACT	6	NOTE	multiple.c	multiple	0	7	FUNCTION_RETURN	helper	5	c:@F@helper	0	0
FACTS
expect 1 -i "$work/multiple-exits.tsv"
grep -q 'P101-ERR-008' "$work/stdout"
cat >"$work/early-return.tsv" <<'FACTS'
P101FACT	6	FUNCTION	early.c	early	0	1	helper	1	0	c:@F@helper	0	20
P101FACT	6	NOTE	early.c	early	0	3	FUNCTION_EARLY_RETURN	helper	5	c:@F@helper	4	8
P101FACT	6	NOTE	early.c	early	0	7	FUNCTION_RETURN	helper	5	c:@F@helper	15	19
FACTS
expect 1 -i "$work/early-return.tsv"
grep -q 'P101-ERR-008' "$work/stdout"
grep -q "not the function's final top-level statement" "$work/stdout"
cat >"$work/single-exit.tsv" <<'FACTS'
P101FACT	6	FUNCTION	single.c	single	0	1	helper	1	0	c:@F@helper	0	0
P101FACT	6	NOTE	single.c	single	0	3	FUNCTION_RETURN	helper	5	c:@F@helper	0	0
FACTS
expect 0 -i "$work/single-exit.tsv"
cat >"$work/call-result.tsv" <<'FACTS'
P101FACT	6	FUNCTION	call-result.c	call-result	0	1	helper	1	0	c:@F@helper	0	20
P101FACT	6	NOTE	call-result.c	call-result	0	3	CALL_NOT_ISOLATED	helper	5	c:@F@helper	4	12
FACTS
expect 1 -i "$work/call-result.tsv"
grep -q 'P101-ERR-009' "$work/stdout"
cat >"$work/main-exit.tsv" <<'FACTS'
P101FACT	6	FUNCTION	main.c	main	0	1	main	1	0	c:@F@main	0	0
P101FACT	6	CALL	main.c	main	0	3	exit	0	0	0	main	c:@F@exit	c:@F@main	0	0
FACTS
expect 0 -i "$work/main-exit.tsv"
expect 2 -i ''
expect 2 -C ''
expect 2 -i facts -C db
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
printf 'P101FACT\t6\tFILE\n' >"$work/malformed.tsv"
expect 2 -i "$work/malformed.tsv"
{
  printf '%4095s' '' | tr ' ' x
  printf 'tail\nP101FACT\t6\tFILE\tx\tx\t0\t0\n'
} >"$work/overlong.tsv"
expect 0 -i "$work/overlong.tsv"
set +e
"$tool" -v "$work/sample.c" >/dev/null 2>&-
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
