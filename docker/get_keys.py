import argparse
import subprocess

def copy_from_container(container_id_or_name: str, src_path: str, host_dest_path: str) -> None:
    """Copy a file or folder from a running or stopped container to the host."""
    cmd = ["docker", "cp", f"{container_id_or_name}:{src_path}", host_dest_path]
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        raise RuntimeError(f"Error copying from container: {result.stderr.strip()}")
    print(f"Successfully copied '{src_path}' to '{host_dest_path}'.")

def copy_from_image(image_name: str, src_path: str, host_dest_path: str) -> None:
    """Copy a file or folder directly from a Docker image to the host."""
    # 1. Create an unstarted dummy container from the image
    create_cmd = ["docker", "create", image_name]
    res = subprocess.run(create_cmd, capture_output=True, text=True)
    
    if res.returncode != 0:
        raise RuntimeError(f"Failed to create temporary container: {res.stderr.strip()}")
    
    temp_container_id = res.stdout.strip()
    
    try:
        # 2. Extract the target path
        copy_from_container(temp_container_id, src_path, host_dest_path)
    finally:
        # 3. Clean up the dummy container
        subprocess.run(["docker", "rm", "-f", temp_container_id], capture_output=True)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Copy keys from a Docker image to the host filesystem.")
    parser.add_argument(
        "--image", "-i",
        default="spire-img:latest",
        help="Docker image name (default: spire-img:latest)"
    )
    parser.add_argument(
        "--dest", "-d",
        default="./prebuilt_keys",
        help="Host destination directory path (default: ./prebuilt_keys)"
    )
    
    args = parser.parse_args()

    copy_from_image(args.image, "/app/spire/prebuilt_keys/.", args.dest)
