#!/bin/bash
set -e

PREFIX="$HOME/.local"
export CPATH="$PREFIX/include:$CPATH"
export LIBRARY_PATH="$PREFIX/lib:$LIBRARY_PATH"
export LD_LIBRARY_PATH="$PREFIX/lib:$LD_LIBRARY_PATH"

# Install libyaml
cd /tmp
git clone --depth=1 https://github.com/yaml/libyaml.git
cd libyaml
./bootstrap || ./configure --prefix="$PREFIX"
make -j$(nproc)
make install

# Install libcyaml
cd /tmp
git clone --depth=1 https://github.com/tlsa/libcyaml.git
cd libcyaml
make -j$(nproc)
make install PREFIX="$PREFIX"

echo "libyaml and libcyaml installed to $PREFIX"
