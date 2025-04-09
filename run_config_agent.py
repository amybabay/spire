import subprocess
import sys
import os

def main():
    # Network identity of this host
    local_ip = "192.168.101.201"

    # Paths to config and executables
    spines_dir = "spines/daemon"
    config_agent_path = "./bin/config_receiver"

    # Launch Spines internal daemon
    spines_cmd = f"cd {spines_dir} && ./spines -p 8100 -c spines_int.conf -I {local_ip}"
    spines_proc = subprocess.Popen(spines_cmd, shell=True, stdout=sys.stdout, stderr=sys.stderr)

    # Launch the config agent
    agent_proc = subprocess.Popen(config_agent_path, shell=True, stdout=sys.stdout, stderr=sys.stderr)

    # Wait for Spines process to exit (blocks script exit)
    spines_proc.communicate()

if __name__ == "__main__":
    main()
