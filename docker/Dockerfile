FROM almalinux:9 AS base

# Install basic packages
RUN dnf install -y vim make gcc g++ iproute-tc

# Install Spire dependencies
RUN dnf install -y openssl-devel flex byacc python

# Install debugging tools
RUN dnf install -y gdb valgrind

# Copy source files
COPY . /app/spire
WORKDIR /app/spire

# Set up config files
RUN cd example_conf; ./install_conf.sh conf_4 

# ===== Build Spire core ONLY ====
FROM base AS core
# Compile Spire core (Spines, Prime, SCADA Master, benchmark)
RUN make core
# Run the script to check and generate keys if needed
RUN python3 /app/spire/docker/check_keys.py
# ===== END core =====

# ===== Build FULL Spire system =====
FROM base AS full
# Install additional dependencies
RUN dnf install -y dnf-plugins-core
RUN dnf config-manager --set-enabled crb
RUN dnf install -y epel-release
RUN dnf install -y bison autoconf automake cmake # needed by OpenPLC
RUN dnf install -y qt5-devel qt5-qtwebengine-devel # needed by pvbrowser
# Compile
RUN printf 'Y\n1\n' | make libs
RUN make
# Run the script to check and generate keys if needed
RUN python3 /app/spire/docker/check_keys.py
# ===== END full =====

#ENTRYPOINT /bin/bash
