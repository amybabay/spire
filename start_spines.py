import subprocess
import socket
import os

def get_my_ip():
    hostname = socket.gethostname()
    return socket.gethostbyname(hostname)

def start_spines(my_ip):
    print(f"Starting Spines on IP: {my_ip}")
    cmd = ["./spines", "-c", "spines_ctrl.conf", "-p", "8100", "-I", my_ip]
    subprocess.Popen(cmd, cwd="/app/spire/spines/daemon")

if __name__ == "__main__":
    ip = get_my_ip()
    start_spines(ip)

    # Hand control over to an interactive shell
    os.execvp("/bin/bash", ["/bin/bash"])
