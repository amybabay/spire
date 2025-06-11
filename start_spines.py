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

def start_scada_master(num):
    cmd = ["./scada_master", str(num), str(num)]
    subprocess.Popen(cmd, cwd="/app/spire/scada_master")

def start_prime(num):
    cmd = ["./prime", "-i", str(num), "-g", str(num)]
    subprocess.Popen(cmd, cwd="/app/spire/prime/bin")
    
def start_plc():
    cmd = ["./openplc", "-m","502"]
    subprocess.Popen(cmd, cwd="/app/spire/plcs/pnnl_plc")

def start_config_agent(num):
    cmd = ["./config_agent", "-h", "goldenrod" + str(num)]
    subprocess.Popen(cmd, cwd="/app/spire/prime/bin")

if __name__ == "__main__":
    
    hostname = get_hostname()
    ip = get_my_ip()

    match = re.match(r"(goldenrod|aster)(\d+)$", hostname)
    if not match:
        print(f"Unrecognized hostname format: {hostname}")
        exit(1)

    prefix = match.group(1)
    num = int(match.group(2))

    if(not disabled):
        # Always run control spines
        start_spines("spines_ctrl.conf", 8200, ip)

        if(not testing_config_agent):
            if 1 <= num <= 4:
                # Internal spines, Prime, and SCADA Master
                start_spines("spines_int.conf", 8100, ip)
                start_prime(num)
                start_scada_master(num)

            if 1 <= num <= 6:
                # External spines
                start_spines("spines_ext.conf", 8120, ip) 
        
    if num == 5:
        start_plc()

    # Keep container alive
    os.execvp("/bin/bash", ["/bin/bash"])
