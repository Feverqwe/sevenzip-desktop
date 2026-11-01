#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fixture="$(mktemp -d "${TMPDIR:-/tmp}/sevenzip-output-test.XXXXXX")"
trap 'rm -rf "$fixture"' EXIT
trap 'printf "Agent output assertion failed at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR
export SEVENZIP_OUTPUT_HELPER="$repo_root/scripts/lib/agent-output.sh"
export TMPDIR="$fixture"

compact_success="$(CODEX_SESSION_ID=test SEVENZIP_SCRIPT_OUTPUT=auto bash -c '
  source "$SEVENZIP_OUTPUT_HELPER"
  sevenzip_setup_output sample
  sevenzip_run_command "printf build details" printf "build details\n"
')"
[[ "$compact_success" == $'sample: running (output buffered; wait for exit)\nsample: OK' ]]
[[ -z "$(find "$fixture" -type f -print -quit)" ]]

# A fast command can finish before its background notifier installs traps.
# Only the shell that owns the buffered log may print its terminal status.
fast_success="$(CODEX_SESSION_ID=test SEVENZIP_SCRIPT_OUTPUT=auto bash -c '
  source "$SEVENZIP_OUTPUT_HELPER"
  sevenzip_setup_output fast
  for ((iteration=0; iteration<64; ++iteration)); do
    sevenzip_run_command "fast command" true
  done
')"
[[ "$fast_success" == $'fast: running (output buffered; wait for exit)\nfast: OK' ]]
[[ -z "$(find "$fixture" -type f -print -quit)" ]]

delayed_output="$(CODEX_SESSION_ID=test SEVENZIP_PROGRESS_DELAY_SECONDS=0.02 bash -c '
  source "$SEVENZIP_OUTPUT_HELPER"
  sevenzip_setup_output sample
  sevenzip_run_command "sleep 0.1" sleep 0.1
')"
[[ "$delayed_output" == $'sample: running (output buffered; wait for exit)\nsample: still running: sleep 0.1\nsample: OK' ]]

forced_compact="$(CODEX_SESSION_ID= SEVENZIP_SCRIPT_OUTPUT=compact bash -c '
  source "$SEVENZIP_OUTPUT_HELPER"
  sevenzip_setup_output sample
  printf "build details\n"
')"
[[ "$forced_compact" == $'sample: running (output buffered; wait for exit)\nsample: OK' ]]

verbose_success="$(CODEX_SESSION_ID=test SEVENZIP_SCRIPT_OUTPUT=verbose bash -c '
  source "$SEVENZIP_OUTPUT_HELPER"
  sevenzip_setup_output sample
  printf "build details\n"
')"
[[ "$verbose_success" == 'build details' ]]

human_success="$(CODEX_SESSION_ID= SEVENZIP_SCRIPT_OUTPUT=auto bash -c '
  source "$SEVENZIP_OUTPUT_HELPER"
  sevenzip_setup_output sample
  printf "build details\n"
')"
[[ "$human_success" == 'build details' ]]

if failure_output="$(CODEX_SESSION_ID=test SEVENZIP_SCRIPT_OUTPUT=auto bash -c '
  source "$SEVENZIP_OUTPUT_HELPER"
  sevenzip_setup_output sample
  printf "first detail\n"
  for number in {1..90}; do printf "line %s\n" "$number"; done
  exit 7
' 2>&1)"; then
  printf 'A failed command unexpectedly succeeded.\n' >&2
  exit 1
else
  status="$?"
fi
[[ "$status" -eq 7 ]]
[[ "$failure_output" == sample:\ running\ \(output\ buffered\;\ wait\ for\ exit\)* ]]
[[ "$failure_output" == *'sample: FAILED (exit 7)'* ]]
[[ "$failure_output" == *'line 90'* ]]
[[ "$failure_output" != *'first detail'* ]]
log_path="${failure_output##*Full log: }"
[[ -f "$log_path" ]]
grep -q '^first detail$' "$log_path"

mkdir -p "$fixture/bin"
cat > "$fixture/bin/ctest" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "${CTEST_PARALLEL_LEVEL:-}" > "$FAKE_CTEST_PARALLEL_LEVEL"
printf '    Start 1: sample\n'
printf '1/2 Test #1: sample ......................   Passed    0.01 sec\n'
printf 'fixture CTest output\n'
if [[ "${FAKE_CTEST_STATUS:-0}" != 0 ]]; then
  printf '2/2 Test #2: failure ..................... ***Failed    0.02 sec\n'
  printf 'failure details\n' >&2
fi
if [[ -n "${FAKE_CTEST_RELEASE:-}" ]]; then
  while [[ ! -f "$FAKE_CTEST_RELEASE" ]]; do sleep 0.02; done
fi
exit "${FAKE_CTEST_STATUS:-0}"
EOF
chmod +x "$fixture/bin/ctest"
export PATH="$fixture/bin:$PATH"
export FAKE_CTEST_PARALLEL_LEVEL="$fixture/ctest-parallel-level"
export SEVENZIP_INCLUDE_SLOW_TESTS=0
ctest_success="$(CTEST_PARALLEL_LEVEL= SEVENZIP_CTEST_JOBS= CODEX_SESSION_ID=test SEVENZIP_SCRIPT_OUTPUT=auto bash \
  "$repo_root/scripts/ctest.sh" --preset dev -R '^agent_output$')"
[[ "$ctest_success" == 'fixture CTest output' ]]

CTEST_PARALLEL_LEVEL= SEVENZIP_CTEST_JOBS=2 CODEX_SESSION_ID=test SEVENZIP_SCRIPT_OUTPUT=auto bash \
  "$repo_root/scripts/ctest.sh" --preset dev --parallel 3 >/dev/null
[[ "$(cat "$FAKE_CTEST_PARALLEL_LEVEL")" == 2 ]]

CTEST_PARALLEL_LEVEL=5 SEVENZIP_CTEST_JOBS=2 CODEX_SESSION_ID=test SEVENZIP_SCRIPT_OUTPUT=auto bash \
  "$repo_root/scripts/ctest.sh" --preset dev >/dev/null
[[ "$(cat "$FAKE_CTEST_PARALLEL_LEVEL")" == 5 ]]

if ctest_failure="$(CODEX_SESSION_ID=test SEVENZIP_SCRIPT_OUTPUT=auto FAKE_CTEST_STATUS=9 bash \
  "$repo_root/scripts/ctest.sh" --preset dev 2>&1)"; then
  printf 'A failed CTest unexpectedly succeeded.\n' >&2
  exit 1
else
  status="$?"
fi
[[ "$status" -eq 9 ]]
[[ "$ctest_failure" == *'fixture CTest output'* ]]
[[ "$ctest_failure" == *'***Failed'* ]]
[[ "$ctest_failure" == *'failure details'* ]]
[[ "$ctest_failure" != *'Start 1:'* && "$ctest_failure" != *'Passed    0.01 sec'* ]]

FAKE_CTEST_RELEASE="$fixture/release-ctest" bash "$repo_root/scripts/ctest.sh" --preset dev \
  >"$fixture/live-output" &
ctest_pid="$!"
for attempt in {1..250}; do
  if grep -q '^fixture CTest output$' "$fixture/live-output"; then break; fi
  sleep 0.02
done
touch "$fixture/release-ctest"
wait "$ctest_pid"
[[ "$attempt" -lt 250 ]]

if sandbox_output="$(CODEX_SANDBOX=seatbelt bash "$repo_root/scripts/check.sh" 2>&1)"; then
  printf 'The full check unexpectedly ran in a sandbox.\n' >&2
  exit 1
else
  status="$?"
fi
[[ "$status" -eq 2 ]]
[[ "$sandbox_output" == 'check.sh: full regression gate requires an unsandboxed run (CODEX_SANDBOX=seatbelt).' ]]

printf 'Agent output keeps successes short and failures diagnosable.\n'
