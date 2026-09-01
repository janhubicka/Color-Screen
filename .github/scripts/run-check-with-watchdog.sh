#!/usr/bin/env bash
set -euo pipefail

jobs="${COLORSCREEN_CHECK_JOBS:-$(nproc)}"
timeout_seconds="${COLORSCREEN_CHECK_TIMEOUT_SECONDS:-4500}"
heartbeat_seconds="${COLORSCREEN_CHECK_HEARTBEAT_SECONDS:-300}"
grace_seconds="${COLORSCREEN_CHECK_GRACE_SECONDS:-15}"
export OMP_NUM_THREADS="${COLORSCREEN_CHECK_OMP_THREADS:-$(nproc)}"
export OMP_DYNAMIC="${OMP_DYNAMIC:-FALSE}"

if (($#)); then
  check_command=("$@")
else
  check_command=(make -j"$jobs" check)
fi

printf -v check_command_display '%q ' "${check_command[@]}"
check_command_display="${check_command_display% }"

check_pid=""
monitor_pid=""
timeout_marker="$(mktemp)"
rm -f "$timeout_marker"

terminate_check() {
  if [[ -n "$check_pid" ]] && kill -0 "$check_pid" 2>/dev/null; then
    kill -TERM -- "-$check_pid" 2>/dev/null || true
  fi
}

cleanup() {
  if [[ -n "$monitor_pid" ]] && kill -0 "$monitor_pid" 2>/dev/null; then
    kill "$monitor_pid" 2>/dev/null || true
  fi
  terminate_check
  rm -f "$timeout_marker"
}
trap cleanup EXIT INT TERM

print_status() {
  local heading="$1"
  echo "::group::$heading"
  printf 'UTC time: '
  date -u '+%Y-%m-%dT%H:%M:%SZ'
  echo "command: $check_command_display"
  echo "configured make jobs: $jobs; OMP_NUM_THREADS: $OMP_NUM_THREADS"
  echo "Processes in the check session:"
  ps -eo pid=,ppid=,sid=,etime=,%cpu=,%mem=,stat=,args= --sort=-%cpu \
    | awk -v sid="$check_pid" '$3 == sid' \
    | head -n 60 || true
  echo "Most recently updated test artifacts:"
  find testsuite src/libcolorscreen -maxdepth 1 -type f \
    \( -name '*.log' -o -name '*.trs' -o -name '*.txt' -o -name '*.par' \) \
    -printf '%T@ %TY-%Tm-%TdT%TH:%TM:%TS %p\n' 2>/dev/null \
    | sort -nr | head -n 25 | cut -d' ' -f2- || true
  echo "::endgroup::"
}

print_log_tails() {
  local heading="$1"
  echo "::group::$heading"
  while read -r logfile; do
    [[ -n "$logfile" ]] || continue
    echo "===== $logfile ====="
    tail -n 100 "$logfile" 2>/dev/null || true
  done < <(find testsuite src/libcolorscreen -maxdepth 1 -type f \
    \( -name '*.log' -o -name '*.trs' \) -printf '%T@ %p\n' 2>/dev/null \
    | sort -nr | head -n 8 | cut -d' ' -f2-)
  echo "::endgroup::"
}

print_backtraces() {
  echo "::group::Backtraces for timed-out test processes"
  if ! command -v gdb >/dev/null 2>&1; then
    echo "gdb is not installed in this CI image; process diagnostics above are the fallback."
    echo "::endgroup::"
    return
  fi

  while read -r pid sid args; do
    [[ "$sid" == "$check_pid" ]] || continue
    case "$args" in
      *colorscreen*|*unittests*)
        echo "===== pid $pid: $args ====="
        timeout 20s gdb -q -batch -nx \
          -ex 'set pagination off' \
          -ex 'thread apply all bt' \
          -p "$pid" 2>&1 || true
        ;;
    esac
  done < <(ps -eo pid=,sid=,args=)
  echo "::endgroup::"
}

monitor_check() {
  local start_seconds=$SECONDS
  local next_heartbeat=$((start_seconds + heartbeat_seconds))

  while kill -0 "$check_pid" 2>/dev/null; do
    local now=$SECONDS
    if (( now - start_seconds >= timeout_seconds )); then
      echo "::error::check command exceeded ${timeout_seconds}s"
      print_status "Timed-out check diagnostics"
      print_log_tails "Timed-out test log tails"
      print_backtraces
      : >"$timeout_marker"

      kill -TERM -- "-$check_pid" 2>/dev/null || true
      local stop_deadline=$((SECONDS + grace_seconds))
      while kill -0 "$check_pid" 2>/dev/null && (( SECONDS < stop_deadline )); do
        sleep 1
      done
      if kill -0 "$check_pid" 2>/dev/null; then
        kill -KILL -- "-$check_pid" 2>/dev/null || true
      fi
      return
    fi

    if (( now >= next_heartbeat )); then
      print_status "check heartbeat"
      next_heartbeat=$((now + heartbeat_seconds))
    fi
    sleep 5
  done
}

echo "Running check command with a ${timeout_seconds}s watchdog."
echo "Command: $check_command_display"
echo "OpenMP threads per test: ${OMP_NUM_THREADS}."
setsid "${check_command[@]}" &
check_pid=$!
monitor_check &
monitor_pid=$!

# The parent shell owns the child wait and performs it exactly once.  The
# monitor only observes the process and kills its session on timeout.  This
# preserves the command's real exit status even if it exits between polls.
set +e
wait "$check_pid"
status=$?
set -e
check_pid=""

if [[ -e "$timeout_marker" ]]; then
  wait "$monitor_pid" 2>/dev/null || true
  status=124
else
  kill "$monitor_pid" 2>/dev/null || true
  wait "$monitor_pid" 2>/dev/null || true
fi
monitor_pid=""

if (( status != 0 && status != 124 )); then
  print_log_tails "Failed test log tails"
fi

rm -f "$timeout_marker"
trap - EXIT INT TERM
exit "$status"
