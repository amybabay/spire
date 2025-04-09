FROM almalinux:latest

# Install basic packages
RUN dnf install -y vim make gcc iproute-tc

# Install Spire dependencies
RUN dnf install -y dnf-plugins-core
RUN dnf config-manager --set-enabled crb
RUN dnf install -y openssl-devel flex byacc qt5-devel cmake python git libyaml-devel

# Install debugging tools
RUN dnf install -y gdb valgrind

# Copy source files
COPY . /app/spire
WORKDIR /app/spire

# Set up config files
RUN cd example_conf; ./install_conf.sh conf_4 

# Install libcyaml from source
RUN git clone https://github.com/tlsa/libcyaml.git /tmp/libcyaml && \
    cd /tmp/libcyaml && \
    make && make install && \
    ldconfig

# Build Spire core (Spines, Prime, SCADA Master, benchmark)
RUN make core

# Run the script to check and generate keys if needed
RUN python3 /app/spire/run_spire.py

#ENTRYPOINT /bin/bash
