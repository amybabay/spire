#!/bin/bash

session="scada_$(date +%Y%m%d_%H%M%S)"
containers=(spire1 spire2 spire3 spire4)

tmux new-session -d -s "$session" -n "scada_master" \
    "docker exec -it ${containers[0]} bash -c 'cd scada_master && ./scada_master 1 1; exec bash'"

for i in {2..4}; do
  index=$((i - 1))
  tmux split-window -t "$session:0" -v
  tmux select-layout -t "$session:0" tiled
  tmux send-keys -t "$session:0.$index" \
    "docker exec -it ${containers[$index]} bash -c 'cd scada_master && ./scada_master $i $i; exec bash'" C-m
done

tmux select-layout -t "$session:0" tiled
tmux attach -t "$session"
