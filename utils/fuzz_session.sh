#!/bin/bash
# tmux session with N windows (default 16) each running fuzz.py; usage: ./fuzz_session.sh [--num N] [fuzz.py options, e.g. --iters 10]

SESSION="fuzzing bosphorus"
DIR="$(dirname "$(realpath "$0")")"
NUM_WINDOWS=16

if [ "$1" = "--num" ]; then
  if [ -z "$2" ] || ! [[ "$2" =~ ^[0-9]+$ ]]; then
    echo "Error: --num requires a positive integer argument"
    exit 1
  fi
  NUM_WINDOWS=$2
  shift 2
fi

DEFAULT_GANAK="$DIR/../../sat_solvers/ganak/build/ganak"
[ -z "$GANAK" ] && [ -x "$DEFAULT_GANAK" ] && GANAK="$(realpath "$DEFAULT_GANAK")"
GANAK="${GANAK:-$(command -v ganak)}"
if [ -z "$GANAK" ]; then
  echo "Error: no ganak: not at $DEFAULT_GANAK nor in PATH, and GANAK is not set"
  exit 1
fi

# GANAK on the command line: a running tmux server does not take this shell's environment
CMD="GANAK=$(printf %q "$GANAK") ./fuzz.py"
[ $# -gt 0 ] && CMD="$CMD $(printf '%q ' "$@")"

if tmux has-session -t "$SESSION" 2>/dev/null; then
  echo "Session '$SESSION' already exists, attaching..."
  tmux attach -t "$SESSION"
  exit 0
fi

echo "Creating tmux session with $NUM_WINDOWS windows..."
tmux new-session -d -s "$SESSION" -c "$DIR" -n "fuzz-1"
tmux send-keys -t "$SESSION:fuzz-1" "$CMD" Enter
for i in $(seq 2 "$NUM_WINDOWS"); do
  tmux new-window -t "$SESSION" -c "$DIR" -n "fuzz-$i"
  tmux send-keys -t "$SESSION:fuzz-$i" "$CMD" Enter
done

tmux select-window -t "$SESSION:fuzz-1"
tmux attach -t "$SESSION"
