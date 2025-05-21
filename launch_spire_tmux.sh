#!/bin/bash

# Start Docker containers
echo "Starting containers..."
docker-compose up -d

# Container names (adjust if needed)
containers=(spire1 spire2 spire3 spire4 spire5 spire6)

# Create new tmux session
session="spire"
tmux new-session -d -s $session

# First container in the first pane
tmux send-keys -t $session "docker exec -it ${containers[0]} bash" C-m

# Split the rest into panes
for i in "${!containers[@]}"; do
  if [[ $i -eq 0 ]]; then
    continue
  fi

  # Alternate vertical and horizontal splits
  if (( i % 2 == 1 )); then
    tmux split-window -h -t $session
  else
    tmux split-window -v -t $session
  fi

  tmux select-pane -t $i
  tmux send-keys "docker exec -it ${containers[$i]} bash" C-m
done

# Evenly distribute panes
tmux select-layout tiled

# Attach to the session
tmux attach-session -t $session
