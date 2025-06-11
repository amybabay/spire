#!/bin/bash

if [ -z "$1" ]; then
    echo "Usage: $0 <num_hosts>"
    exit 1
fi

NUM_HOSTS=$1
SESSION="tail_sm"

# Start new tmux session
tmux new-session -d -s $SESSION

# First pane (aster1)
tmux send-keys -t $SESSION "docker exec -it aster1 bash -c 'tail -f prime/bin/logs/sm.log'" C-m

# Remaining panes (aster2 to asterN)
for ((i=2; i<=NUM_HOSTS; i++)); do
    tmux split-window -t $SESSION
    tmux select-layout -t $SESSION tiled
    tmux send-keys -t $SESSION "docker exec -it aster$i bash -c 'tail -f prime/bin/logs/sm.log'" C-m
done

# Attach to session
tmux attach-session -t $SESSION
