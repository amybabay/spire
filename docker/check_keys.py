import os
import subprocess
import shutil

PREBUILT_DIR = "/app/spire/prebuilt_keys"

KEY_CONFIGS = {
    "spines": {
        "prebuilt": f"{PREBUILT_DIR}/spines",
        "actual": "/app/spire/spines/daemon/keys",
        "gen_cmd": "cd /app/spire/spines/daemon && bash gen_keys.sh",
        "check_fn": lambda p: os.path.exists(p) and bool(os.listdir(p)),
        "copy_src": "/app/spire/spines/daemon/keys",
    },
    "scada": {
        "prebuilt": f"{PREBUILT_DIR}/scada",
        "actual": "/app/spire/scada_master/sm_keys",
        "gen_cmd": "cd /app/spire/scada_master && ./gen_keys",
        "check_fn": lambda p: os.path.exists(p) and any(
            f.startswith("share") or f.startswith("pubkey_") for f in os.listdir(p)
        ),
        "copy_src": "/app/spire/scada_master/sm_keys",
    },
    "prime_rsa": {
        "prebuilt": f"{PREBUILT_DIR}/prime/rsa",
        "actual": "/app/spire/prime/bin/keys",
        "gen_cmd": "cd /app/spire/prime/bin && ./gen_keys -r",
        "check_fn": lambda p: os.path.exists(p) and any(
            f.startswith("public_") or f.startswith("private_") for f in os.listdir(p)
        ),
        "copy_src": "/app/spire/prime/bin/keys",
        "file_filter": lambda f: f.startswith("public_") or f.startswith("private_"),
    },
    "prime_tc": {
        "prebuilt": f"{PREBUILT_DIR}/prime/tc",
        "actual": "/app/spire/prime/bin/keys",
        "gen_cmd": "cd /app/spire/prime/bin && ./gen_keys -t",
        "check_fn": lambda p: os.path.exists(p) and any(
            f.startswith("share") or f.startswith("pubkey_") for f in os.listdir(p)
        ),
        "copy_src": "/app/spire/prime/bin/keys",
        "file_filter": lambda f: f.startswith("share") or f.startswith("pubkey"),
    },
    "prime_tpm": {
        "prebuilt": f"{PREBUILT_DIR}/prime/tpm_keys",
        "actual": "/app/spire/prime/bin/tpm_keys",
        "gen_cmd": "cd /app/spire/prime/bin && ./gen_tpm_keys.sh",
        "check_fn": lambda p: os.path.exists(p) and bool(os.listdir(p)),
        "copy_src": "/app/spire/prime/bin/tpm_keys",
    },
}

def copy_prebuilt_keys():
    """Copy prebuilt keys to actual locations if actual keys are missing."""
    print("[INFO] Checking for available prebuilt keys...")
    for key_type, cfg in KEY_CONFIGS.items():
        src_path = cfg["prebuilt"]
        dest_path = cfg["actual"]

        if os.path.exists(src_path) and os.listdir(src_path):
            if not cfg["check_fn"](dest_path):
                print(f"[INFO] Copying prebuilt {key_type} keys from {src_path} to {dest_path}")
                os.makedirs(dest_path, exist_ok=True)
                file_filter = cfg.get("file_filter")
                for file in os.listdir(src_path):
                    if file_filter is None or file_filter(file):
                        shutil.copy(os.path.join(src_path, file), dest_path)
        else:
            print(f"[INFO] No prebuilt keys found for {key_type}.")

def check_and_generate_keys():
    """Check each key set individually and generate only missing key categories."""
    for key_type, cfg in KEY_CONFIGS.items():
        actual_path = cfg["actual"]

        if cfg["check_fn"](actual_path):
            print(f"[INFO] Keys for '{key_type}' are present.")
            continue

        print(f"[WARNING] Missing keys for '{key_type}'. Generating...")
        try:
            subprocess.run(cfg["gen_cmd"], shell=True, check=True)

            # Copy newly generated keys to prebuilt storage for future runs
            prebuilt_path = cfg["prebuilt"]
            src_path = cfg["copy_src"]
            os.makedirs(prebuilt_path, exist_ok=True)

            if os.path.exists(src_path):
                file_filter = cfg.get("file_filter")
                for file in os.listdir(src_path):
                    if file_filter is None or file_filter(file):
                        shutil.copy(os.path.join(src_path, file), prebuilt_path)
                print(f"[INFO] Saved generated '{key_type}' keys to prebuilt directory: {prebuilt_path}")
        except subprocess.CalledProcessError as e:
            print(f"[ERROR] Failed to generate keys for '{key_type}': {e}")
            exit(1)

if __name__ == "__main__":
    copy_prebuilt_keys()
    check_and_generate_keys()
    print("[INFO] Key verification and generation complete.")
