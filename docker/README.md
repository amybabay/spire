# Docker Setup

## Building the image and generating keys

Because key generation takes a long time, if you are doing development work,
you probably want to avoid re-generating them every time you re-build the
Docker image. If you already have generated keys, place them in
`prebuilt_keys/prime`, `prebuilt_keys/scada`, and `prebuilt_keys/spines`.

Build image using Dockerfile. This process uses the `check_keys.py` script to
check if there are already pre-generated keys; if so, it copies them to the
correct locations. If keys are missing, it will generate them.

From **top-level Spire directory**:
```
docker build -f docker/Dockerfile -t spire-img .
```

If the build command needed to generate the keys, you can copy them to your
host with the following command (also run from the top-level Spire directory):
```
python3 docker/get_keys.py
```

## Benchmarking / Core Functionality Testing

Run replicas and benchmark container, but don't start benchmark process (run
from spire/docker directory):
```
docker compose --profile benchmark up -d
```

Then, you can open an interactive window on the client and start benchmarks (-n
parameter controls the number of clients to start, can use -u to control the
number of updates per client):
```
docker exec -it spire-benchmark bash
python docker/run_benchmark.py -n 1
```

When done:
```
docker compose --profile benchmark down
```

## Running with PLCs/HMIs

Run all processes (from spire/docker directory):
```
docker compose --profile full up -d
```

Open pvbrower window (on host). You should be able to access HMI GUIs at
`pv://localhost:5051`, `pv://localhost:5052`, `pv://localhost:5053`

Note: If running pvbrowser in a VM rather than natively on host, you may need
to adjust your VM network settings. What worked for me in VMWare was using the
"Host-Only" network adapter, running `ip a` in the VM to get its IP address
(happened to be 192.168.106.128) and then using the gateway for that network
(192.168.106.1) as the IP address for pvbrowser to connect to instead of
localhost.

When done:
```
docker compose --profile full down
```

## Other stuff

### To run standalone container (debugging)

This can be useful for debugging build process (e.g., comment out `make`
commands in Dockerfile, and build interactively in container).

Example creating, running, and opening an interactive terminal session on a
container named "spire" using the built image "spire-img":
```
docker run -it --name spire spire-img
```

To run it with a specific IP address (e.g. so that the default configuration
works), you need to create a Docker network and specify the IP address when
running:
```
docker network create --subnet 192.168.101.0/24 spire-net
```

```
docker run -it --net spire-net --ip 192.168.101.101 --name spire spire-img
```

### Interactive benchmarking / functionality checking

This is just giving some more manual options to accomplish the same as in the
main benchmarking instructions.

Run replicas only (from `spire/docker` directory):
```
docker compose up -d
```

To check log files:
```
docker compose logs
```

To manually add a benchmark client, first start up a container with (external)
Spines running:
```
docker compose up -d benchmark-client
```

Then, you can manually open a window on the client with:
```
docker exec -it spire-benchmark bash
```

and start a benchmark with (manual version):
```
cd benchmark && ./benchmark 1 192.168.101.107:8120 1000000 100
```

To start another benchmark client, open another window and give the new client
a new ID:
```
docker exec -it spire-benchmark bash
cd benchmark && ./benchmark 2 192.168.101.107:8120 1000000 100
```

Or you can use a script-wrapped version that can start multiple clients at once
(in the background) by changing -n parameter to be desired number of benchmark
clients:
```
python docker/run_benchmark.py -n 1
```
