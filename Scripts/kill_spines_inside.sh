#!/bin/bash

# This script is meant to be executed *inside* a container

for conf in spines_int.conf spines_ext.conf; do
    pids=$(ps aux | grep "$conf" | grep -v grep | awk '{print $2}')
    for pid in $pids; do
        kill -9 "$pid"
    done
done
