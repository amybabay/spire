import subprocess
import socket
import os
import re

testing_config_agent = True
disabled = False


def get_hostname():
    return os.getenv("MY_HOSTNAME", "")

def get_my_ip():
    hostname = get_hostname()
    return socket.gethostbyname(hostname)

def start_spines(config_file, port, ip):
    cmd = ["./spines", "-c", config_file, "-p", str(port), "-I", ip]
    subprocess.Popen(cmd, cwd="/app/spire/spines/daemon")

def start_plc():
    cmd = ["./openplc", "-m","502"]
    subprocess.Popen(cmd, cwd="/app/spire/plcs/pnnl_plc")

def start_config_agent(num):
    cmd = ["./config_agent", "-h", "goldenrod" + str(num)]
    subprocess.Popen(cmd, cwd="/app/spire/prime/bin")

if __name__ == "__main__":
    
    hostname = get_hostname()
    ip = get_my_ip()

    match = re.match(r"aster(\d+)$", hostname)
    if not match:
        print(f"Unrecognized hostname format: {hostname}")
        exit(1)

    num = int(match.group(1))

    start_spines("spines_ctrl.conf", 8200, ip)

    if num == 19:
        start_plc()

    # Keep container alive
    os.execvp("/bin/bash", ["/bin/bash"])
