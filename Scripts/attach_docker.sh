#!/bin/bash

if [ -z "$1" ]; then
    echo "Usage: $0 <num_hosts>"
    exit 1
fi

NUM_HOSTS=$1

SESSION="attach_docker"

# Start a new tmux session with the first pane
tmux new-session -d -s $SESSION -n "asters" "docker exec -it aster1 bash"

# Split horizontally for aster2–aster6
for ((i=2; i<=NUM_HOSTS; i++)); do
    tmux split-window -t $SESSION:0 -h "docker exec -it aster$i bash"
    tmux select-layout -t $SESSION:0 tiled
done

# Focus on the first pane
tmux select-pane -t $SESSION:0.0

# Attach to the session
tmux attach -t $SESSION
