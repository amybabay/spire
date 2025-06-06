#!/bin/bash

SESSION="goldenrod_exec"

# Start a new tmux session with the first pane
tmux new-session -d -s $SESSION -n "goldenrods" "docker exec -it goldenrod1 bash"

# Split horizontally for goldenrod2–goldenrod6
for i in {2..6}; do
    tmux split-window -t $SESSION:0 -h "docker exec -it goldenrod$i bash"
    tmux select-layout -t $SESSION:0 tiled
done

# Focus on the first pane
tmux select-pane -t $SESSION:0.0

# Attach to the session
tmux attach -t $SESSION
