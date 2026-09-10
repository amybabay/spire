import sys, argparse, subprocess, os

base_dir = "/app/spire"
log_dir = f"{base_dir}/logs"

def get_args(argv):
    parser = argparse.ArgumentParser(description="Run N benchmark clients")
    parser.add_argument('-n', '--num-clients', required=True, type=int, help='Number of benchmark clients to run')
    parser.add_argument('-u', '--updates', type=int, default=100, help='Number of updates to complete (default: 100)')
    parser.add_argument('--ip', default='192.168.101.107', help='Target Spines external IP address (default: 192.168.101.107)')
    parser.add_argument('--port', type=int, default=8120, help='Target Spines external port (default: 8120)')
    return parser.parse_args(argv)

def run_cmd(cmd: str, tag: str, log_file: str) -> subprocess.Popen:
    """Run shell command, prefixing each line with [tag] before streaming to stdout and log file."""
    os.makedirs(log_dir, exist_ok=True)
    tee_cmd = f"{cmd} 2>&1 | sed -u 's/^/[{tag}] /' | tee -a {log_file}"
    return subprocess.Popen(tee_cmd, shell=True)

def main(argv):
    args = get_args(argv)
    n = args.num_clients
    updates = args.updates
    target_ip = args.ip
    port = args.port

    procs = []
    for i in range(1, n + 1):
        bench_cmd = f"cd {base_dir}/benchmark && ./benchmark {i} {target_ip}:{port} 1000000 {updates}"
        tag = f"bench_{i}"
        log_file = f"{log_dir}/out_bench_{i}.txt"
        
        proc = run_cmd(bench_cmd, tag, log_file)
        procs.append(proc)

    # Wait for all benchmark processes to complete
    for p in procs:
        p.communicate()

if __name__ == "__main__":
    main(sys.argv[1:])
