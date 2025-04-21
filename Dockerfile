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

# Install libcyaml from source
RUN git clone https://github.com/tlsa/libcyaml.git /tmp/libcyaml && \
    cd /tmp/libcyaml && \
    make && make install && \
    ldconfig

RUN echo "/usr/local/lib" > /etc/ld.so.conf.d/local.conf && ldconfig

# Build Spire core
RUN make core

# Run setup during build
RUN python3 /app/spire/check_keys.py && \
    cd /app/spire/example_conf && ./install_conf.sh conf_4

# When container starts, just drop into shell
# CMD ["/bin/bash"]

COPY start_spines.py /app/spire/start_spines.py

# Set entrypoint to python script
ENTRYPOINT ["python3", "/app/spire/start_spines.py"]

