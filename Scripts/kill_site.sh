#!/bin/bash

# Usage: ./run_kill_spines.sh <container_name>

CONTAINER="$1"

if [ -z "$CONTAINER" ]; then
    echo "Usage: $0 <container_name>"
    exit 1
fi

# Execute it inside the container
docker exec "$CONTAINER" bash /app/spire/Scripts/kill_spines_inside.sh
