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

## To run standalone container

This can be useful for debugging build process (e.g., comment out `make`
commands in Dockerfile, and build interactively in container).

Example creating, running, and opening an interactive terminal session on a
container named "spire" using the built image "spire-img":
```
docker run -it --name spire spire-img
```
