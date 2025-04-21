import subprocess
import os

print("Running check_keys.py...")
subprocess.run(["python3", "/app/spire/check_keys.py"], check=True)

print("Running install_conf.sh...")
os.chdir("/app/spire/example_conf")
subprocess.run(["./install_conf.sh", "conf_4"], check=True)

print("Setup complete.")
