#!/bin/bash

# Usage: ./launch-spire.sh <IP_ADDRESS> <HOSTNAME>
IP="$1"
HOSTNAME="$2"

if [[ -z "$IP" || -z "$HOSTNAME" ]]; then
    echo "Usage: $0 <IP_ADDRESS> <HOSTNAME>"
    exit 1
fi

SESSION="spire"

# Determine absolute project root path from this script's location
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Start tmux session with one window
tmux new-session -d -s "$SESSION" -n "spire"

# --- Pane 0: spines ---
tmux send-keys -t "$SESSION:0.0" "cd \"$PROJECT_ROOT/spines/daemon\"" C-m
tmux send-keys -t "$SESSION:0.0" "LD_LIBRARY_PATH=\$HOME/.local/lib:\$LD_LIBRARY_PATH ./spines -c spines_ctrl.conf -p 8200 -I $IP" C-m

sleep 1

# --- Pane 1: config_agent ---
tmux split-window -v -t "$SESSION:0"
tmux send-keys -t "$SESSION:0.1" "cd \"$PROJECT_ROOT/prime/bin\"" C-m
tmux send-keys -t "$SESSION:0.1" "LD_LIBRARY_PATH=\$HOME/.local/lib:\$LD_LIBRARY_PATH ./config_agent -a $IP -h $HOSTNAME -l 1" C-m

# Optional: focus back on pane 0
tmux select-pane -t "$SESSION:0.0"

echo "Launched Spines and Config Agent in tmux session '$SESSION' (two panes)"
