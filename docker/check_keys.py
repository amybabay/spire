import os
import subprocess
import shutil

# Paths to key storage and actual usage locations
prebuilt_keys_paths = {
    "spines": "/app/spire/prebuilt_keys/spines",
    "prime": "/app/spire/prebuilt_keys/prime",
    "scada": "/app/spire/prebuilt_keys/scada",
}

actual_keys_paths = {
    "spines": "/app/spire/spines/daemon/keys",
    "prime": "/app/spire/prime/bin/keys",
    "scada": "/app/spire/scada_master/sm_keys",
}

def copy_keys():
    """Copy prebuilt keys to their correct locations if missing."""
    print("[INFO] Copying prebuilt keys to correct locations...")

    for key_type, src_path in prebuilt_keys_paths.items():
        dest_path = actual_keys_paths[key_type]

        if os.path.exists(src_path) and os.listdir(src_path):  # Ensure prebuilt keys exist
            print(f"[INFO] Copying {key_type} keys from {src_path} to {dest_path}")
            os.makedirs(dest_path, exist_ok=True)
            for file in os.listdir(src_path):
                shutil.copy(os.path.join(src_path, file), dest_path)
        else:
            print(f"[WARNING] No prebuilt keys found for {key_type}, will attempt to generate.")

def check_key_files():
    """Check if all required key directories contain files."""
    missing_keys = False
    for name, path in actual_keys_paths.items():
        if not os.path.exists(path) or not os.listdir(path):
            print(f"[ERROR] Missing or empty key directory: {path}")
            missing_keys = True
    return not missing_keys

def generate_keys():
    """Generate missing keys if they were not found in prebuilt_keys."""
    print("[INFO] Generating missing keys...")

    try:
        print("[INFO] Running Spines key generation...")
        subprocess.run("cd /app/spire/spines/daemon && bash gen_keys.sh", shell=True, check=True)
        subprocess.run(f"mkdir -p {prebuilt_keys_paths['spines']} && cp -r /app/spire/spines/daemon/keys/* {prebuilt_keys_paths['spines']}/", shell=True, check=True)

        print("[INFO] Running Prime key generation...")
        subprocess.run("cd /app/spire/prime/bin && ./gen_keys && ./gen_tpm_keys.sh", shell=True, check=True)
        subprocess.run(f"mkdir -p {prebuilt_keys_paths['prime']} && cp -r /app/spire/prime/bin/keys/* {prebuilt_keys_paths['prime']}/", shell=True, check=True)

        print("[INFO] Running SCADA Master key generation...")
        subprocess.run("cd /app/spire/scada_master && ./gen_keys", shell=True, check=True)
        subprocess.run(f"mkdir -p {prebuilt_keys_paths['scada']} && cp -r /app/spire/scada_master/sm_keys/* {prebuilt_keys_paths['scada']}/", shell=True, check=True)

    except subprocess.CalledProcessError as e:
        print(f"[ERROR] Key generation failed: {e}")
        exit(1)

if __name__ == "__main__":
    copy_keys()  # First, copy prebuilt keys if available

    if not check_key_files():
        generate_keys()  # Generate keys only if they are still missing
    else:
        print("[INFO] All keys are present. Skipping key generation.")
