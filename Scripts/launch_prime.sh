#!/bin/bash

session="prime_$(date +%Y%m%d_%H%M%S)"
containers=(spire1 spire2 spire3 spire4)

tmux new-session -d -s "$session" -n "prime" \
    "docker exec -it ${containers[0]} bash -c 'cd prime/bin && ./prime -i 1 -g 1; exec bash'"

for i in {2..4}; do
  index=$((i - 1))
  tmux split-window -t "$session:0" -v
  tmux select-layout -t "$session:0" tiled
  tmux send-keys -t "$session:0.$index" \
    "docker exec -it ${containers[$index]} bash -c 'cd prime/bin && ./prime -i $i -g $i; exec bash'" C-m
done

tmux select-layout -t "$session:0" tiled
tmux attach -t "$session"
