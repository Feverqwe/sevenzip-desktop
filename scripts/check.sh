#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"
source "$project_root/scripts/lib/toolchain.sh"
sevenzip_select_toolchain dev

case "${CODEX_SANDBOX:-}" in
  ''|none|off|disabled|0) ;;
  *)
    printf 'check.sh: full regression gate requires an unsandboxed run (CODEX_SANDBOX=%s).\n' "$CODEX_SANDBOX" >&2
    exit 2
    ;;
esac

source "$project_root/scripts/lib/agent-output.sh"
sevenzip_setup_output 'check.sh (format)'

if [[ "$#" -ne 0 ]]; then
  printf 'Usage: %s\n' "$0" >&2
  exit 2
fi

check_formatting() {
  local format_status=0 format_exit
  if ! command -v clang-format >/dev/null 2>&1; then
    printf 'clang-format is required for the regression gate.\n' >&2
    return 1
  fi

  for source_dir in src tests; do
    if [[ -d "$source_dir" ]]; then
      while IFS= read -r -d '' source_file; do
        if clang-format --dry-run --Werror "$source_file"; then
          :
        else
          format_exit=$?
          if [[ "$format_status" == 0 ]]; then format_status="$format_exit"; fi
        fi
      done < <(find "$source_dir" -type f \( -name '*.cpp' -o -name '*.cppm' -o -name '*.mm' -o -name '*.h' -o -name '*.hpp' \) -print0)
    fi
  done
  if [[ "$format_status" != 0 ]]; then return "$format_status"; fi

  # Runner snapshots already passed both Git whitespace checks locally before
  # source synchronization. The guest needs no project Git repository.
  if [[ -z "${SEVENZIP_RUN_TOKEN:-}" ]]; then
    git diff --check || return
    git diff --cached --check
  fi
}

sevenzip_run_command 'clang-format and source whitespace checks' check_formatting
if [[ -n "${sevenzip_output_log:-}" ]]; then
  trap - EXIT
  sevenzip_finish_output 0
  exec 1>&3 2>&4
fi
./scripts/build.sh dev all
./scripts/ctest.sh --preset dev --output-on-failure
