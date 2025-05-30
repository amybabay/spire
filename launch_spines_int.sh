#!/bin/bash

session="spines_int_$(date +%Y%m%d_%H%M%S)"
containers=(spire1 spire2 spire3 spire4)
ips=(192.168.101.101 192.168.101.102 192.168.101.103 192.168.101.104)

tmux new-session -d -s "$session" -n "spines_int" \
    "docker exec -it ${containers[0]} bash -c 'cd spines/daemon && ./spines -c spines_int.conf -p 8100 -I ${ips[0]}; exec bash'"

for i in {1..3}; do
  tmux split-window -t "$session:0" -v
  tmux select-layout -t "$session:0" tiled
  tmux send-keys -t "$session:0.$i" \
    "docker exec -it ${containers[$i]} bash -c 'cd spines/daemon && ./spines -c spines_int.conf -p 8100 -I ${ips[$i]}; exec bash'" C-m
done

tmux select-layout -t "$session:0" tiled
tmux attach -t "$session"
