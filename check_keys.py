import os
import shutil
import subprocess
import filecmp

# Persistent storage (bind-mounted to host)
pregenerated_keys_path = "/app/spire/pregenerated_keys"

# Paths for actual key locations where services expect them
actual_keys_paths = {
    "spines": "/app/spire/spines/daemon/keys",
    "prime": "/app/spire/prime/bin/keys",
    "scada": "/app/spire/scada_master/sm_keys",
}

# Compares if files in two directories are identical
def are_files_identical(src_dir, dest_dir):
    return filecmp.dircmp(src_dir, dest_dir).left_only == [] and filecmp.dircmp(src_dir, dest_dir).right_only == []

# Restores keys from pregenerated storage if they exist and are not identical to current keys
def restore_keys():
    print("Checking for existing keys in pregenerated storage...")

    for key_type, dest_path in actual_keys_paths.items():
        src_path = os.path.join(pregenerated_keys_path, key_type)

        if os.path.exists(src_path) and os.listdir(src_path):  # Ensure pregenerated keys exist
            # Only restore if the files are different or the target path is empty
            if not os.path.exists(dest_path) or not are_files_identical(src_path, dest_path):
                print(f"Restoring {key_type} keys from {src_path} to {dest_path}")
                os.makedirs(dest_path, exist_ok=True)
                for file in os.listdir(src_path):
                    shutil.copy(os.path.join(src_path, file), dest_path)
            else:
                print(f"Keys for {key_type} already exist and are identical in both locations, skipping restore.")
        else:
            print(f"No pregenerated keys found for {key_type}, will generate if needed.")

# Checks if all required key directories are populated.
def check_key_files():
    missing_keys = False
    for name, path in actual_keys_paths.items():
        if not os.path.exists(path) or not os.listdir(path):
            print(f"Missing or empty key directory: {path}")
            missing_keys = True
    return not missing_keys

# Generates missing keys and saves them to pregenerated storage.
def generate_keys():
    print("Generating missing keys...")

    try:
        print("Generating Spines keys...")
        subprocess.run("cd /app/spire/spines/daemon && bash gen_keys.sh", shell=True, check=True)

        print("Generating Prime keys...")
        subprocess.run("cd /app/spire/prime/bin && ./gen_keys && ./gen_tpm_keys.sh", shell=True, check=True)

        print("Generating SCADA Master keys...")
        subprocess.run("cd /app/spire/scada_master && ./gen_keys", shell=True, check=True)

        # Ensure pregenerated storage exists
        os.makedirs(pregenerated_keys_path, exist_ok=True)

        # Copy generated keys to pregenerated storage
        for key_type, key_path in actual_keys_paths.items():
            dest = os.path.join(pregenerated_keys_path, key_type)
            os.makedirs(dest, exist_ok=True)
            for file in os.listdir(key_path):
                shutil.copy(os.path.join(key_path, file), dest)

        print("Generated keys saved to bind mounted storage.")

    except subprocess.CalledProcessError as e:
        print(f"Key generation failed: {e}")
        exit(1)

if __name__ == "__main__":
    restore_keys()  # Attempt to restore keys from the mount

    if not check_key_files():
        generate_keys()  # Generate only if keys are still missing
    else:
        print("All keys are present. Skipping key generation.")
