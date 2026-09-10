import sys, argparse, subprocess, time, os

base_dir = "/app/spire"
log_dir = f"{base_dir}/logs"

def get_args(argv):
    parser = argparse.ArgumentParser(description="Run a Spire client")
    parser.add_argument(
        '--type', '-t',
        choices=['benchmark', 'plc', 'hmi'],
        default='benchmark',
        help='Client type: benchmark, plc, or hmi (default: benchmark)'
    )
    parser.add_argument(
        '--reconf', '-r',
        action='store_true',
        help='Enable reconfiguration support by running the Spines control network and Config Agent'
    )
    return parser.parse_args(argv)

def run_cmd(cmd: str, tag: str, log_file: str) -> subprocess.Popen:
    """Run shell command, prefixing each line with [tag] before streaming to stdout and log file."""
    os.makedirs(log_dir, exist_ok=True)
    tee_cmd = f"{cmd} 2>&1 | sed -u 's/^/[{tag}] /' | tee -a {log_file}"
    return subprocess.Popen(tee_cmd, shell=True)

def main(argv):
    args = get_args(argv)

    # Determine IPC path and application count based on client type
    if args.type == 'plc':
        i = 7 # assume plc/benchmark is always ID 7, running on 192.168.101.107
        num_apps = 10
        ipc_path = "/tmp/rtu_ipc_main"
    elif args.type == 'hmi':
        i = 8 # assume hmi is always ID 8, running on 192.168.101.108
        num_apps = 3
        ipc_path = "/tmp/hmi_ipc_main"
    else: #benchmark
        i = 7 # assume plc/benchmark is always ID 7, running on 192.168.101.107
        num_apps = 10
        ipc_path = "/tmp/bm_ipc_main"
        args.type = "benchmark"

    ip = f"192.168.101.{100 + i}"

    # Command definitions
    spines_ext_cmd = f"cd {base_dir}/spines/daemon && ./spines -p 8120 -c spines_ext.conf -I {ip}"

    # Launch external spines process
    sp_proc = run_cmd(spines_ext_cmd, f"spines_ext_{args.type}", f"{log_dir}/out_spines_ext_{args.type}.txt")

    # Conditionally launch reconfiguration processes
    if args.reconf:
        spines_ctrl_cmd = f"cd {base_dir}/spines/daemon && ./spines -p 8900 -c spines_ctrl.conf"
        conf_agent_cmd  = f"cd {base_dir}/prime/bin && ./config_agent {i} {ip} {ipc_path} p {num_apps}"

        run_cmd(spines_ctrl_cmd, f"spines_ctrl_{args.type}", f"{log_dir}/out_spines_ctrl_{args.type}.txt")
        time.sleep(5)
        run_cmd(conf_agent_cmd, f"conf_agent_{args.type}", f"{log_dir}/out_conf_agent_{args.type}.txt")

    # Wait for external spines process to exit
    sp_proc.communicate()

if __name__ == "__main__":
    main(sys.argv[1:])
