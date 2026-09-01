#!/usr/bin/env bash
set -euo pipefail

jobs="${COLORSCREEN_CHECK_JOBS:-$(nproc)}"
timeout_seconds="${COLORSCREEN_CHECK_TIMEOUT_SECONDS:-4500}"
heartbeat_seconds="${COLORSCREEN_CHECK_HEARTBEAT_SECONDS:-300}"
grace_seconds="${COLORSCREEN_CHECK_GRACE_SECONDS:-15}"
export OMP_NUM_THREADS="${COLORSCREEN_CHECK_OMP_THREADS:-$(nproc)}"
export OMP_DYNAMIC="${OMP_DYNAMIC:-FALSE}"

check_pid=""

terminate_check() {
  if [[ -n "$check_pid" ]] && kill -0 "$check_pid" 2>/dev/null; then
    kill -TERM -- "-$check_pid" 2>/dev/null || true
  fi
}
trap terminate_check EXIT INT TERM

print_status() {
  local heading="$1"
  echo "::group::$heading"
  printf 'UTC time: '
  date -u '+%Y-%m-%dT%H:%M:%SZ'
  echo "make jobs: $jobs; OMP_NUM_THREADS: $OMP_NUM_THREADS"
  echo "Processes in the check session:"
  ps -eo pid=,ppid=,sid=,etime=,%cpu=,%mem=,stat=,args= --sort=-%cpu \
    | awk -v sid="$check_pid" '$3 == sid' \
    | head -n 60 || true
  echo "Most recently updated test artifacts:"
  find testsuite -maxdepth 1 -type f \
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
  done < <(find testsuite -maxdepth 1 -type f \
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

echo "Running make check with a ${timeout_seconds}s watchdog."
echo "Automake parallelism: -j${jobs}; OpenMP threads per test: ${OMP_NUM_THREADS}."
setsid make -j"$jobs" check &
check_pid=$!
start_seconds=$SECONDS
next_heartbeat=$((start_seconds + heartbeat_seconds))

while kill -0 "$check_pid" 2>/dev/null; do
  now=$SECONDS
  if (( now - start_seconds >= timeout_seconds )); then
    echo "::error::make check exceeded ${timeout_seconds}s"
    print_status "Timed-out make check diagnostics"
    print_log_tails "Timed-out test log tails"
    print_backtraces

    kill -TERM -- "-$check_pid" 2>/dev/null || true
    stop_deadline=$((SECONDS + grace_seconds))
    while kill -0 "$check_pid" 2>/dev/null && (( SECONDS < stop_deadline )); do
      sleep 1
    done
    if kill -0 "$check_pid" 2>/dev/null; then
      kill -KILL -- "-$check_pid" 2>/dev/null || true
    fi
    wait "$check_pid" 2>/dev/null || true
    check_pid=""
    trap - EXIT INT TERM
    exit 124
  fi

  if (( now >= next_heartbeat )); then
    print_status "make check heartbeat"
    next_heartbeat=$((now + heartbeat_seconds))
  fi
  sleep 5
done

set +e
wait "$check_pid"
status=$?
set -e
check_pid=""
trap - EXIT INT TERM
if (( status != 0 )); then
  print_log_tails "Failed test log tails"
fi
exit "$status"
