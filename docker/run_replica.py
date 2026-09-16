import sys, argparse, subprocess, time, os

base_dir = "/app/spire"
log_dir = f"{base_dir}/logs"

def get_args(argv):
    parser = argparse.ArgumentParser(description="Run a Spire replica with given ID")
    parser.add_argument('-id', required=True, type=int, help='Replica ID (1 to num_replicas)')
    parser.add_argument('-ip', required=False, type=str, help='Replica IP (default: 192.168.101.{100+ID})')
    parser.add_argument('--reconf', '-r', action='store_true', help='Enable reconfiguration support by running the Spines control network and Config Agent')
    return parser.parse_args()

def run_cmd(cmd: str, tag: str, log_file: str) -> subprocess.Popen:
    """Run shell command, prefixing each line with [tag] before streaming to stdout and log file."""
    print(cmd)
    os.makedirs(log_dir, exist_ok=True)
    # 2>&1 combines stderr with stdout
    # sed -u prepends the tag instantly without buffering output
    tee_cmd = f"{cmd} 2>&1 | sed -u 's/^/[{tag}] /' | tee -a {log_file}"
    return subprocess.Popen(tee_cmd, shell=True)

def main(argv):
    args = get_args(argv)

    i = args.id
    if args.ip:
        ip = args.ip
    else:
        ip = f"192.168.101.{100 + i}"

    # Set up commands for Spire replica processes
    spines_int_cmd  = f"cd {base_dir}/spines/daemon && ./spines -p 8100 -c spines_int.conf -I {ip}"
    spines_ext_cmd  = f"cd {base_dir}/spines/daemon && ./spines -p 8120 -c spines_ext.conf -I {ip}"
    sm_cmd          = f"cd {base_dir}/scada_master && ./scada_master {i} {i} {ip}:8100 {ip}:8120"
    prime_cmd       = f"cd {base_dir}/prime/bin && ./prime -i {i} -g {i}"

    # Run processes
    sp_proc = run_cmd(spines_int_cmd, f"spines_int_{i}", f"{log_dir}/out_spines_int_{i}.txt")
    run_cmd(spines_ext_cmd, f"spines_ext_{i}", f"{log_dir}/out_spines_ext_{i}.txt")
    time.sleep(5)
    run_cmd(sm_cmd, f"scada_master_{i}", f"{log_dir}/out_sm_{i}.txt")
    run_cmd(prime_cmd, f"prime_{i}", f"{log_dir}/out_prime_{i}.txt")

    # Set up and run optional processes to support reconfiguration
    if args.reconf:
        spines_ctrl_cmd = f"cd {base_dir}/spines/daemon && ./spines -p 8900 -c spines_ctrl.conf -I {ip}"
        conf_agent_cmd  = f"cd {base_dir}/prime/bin && ./config_agent {i} {ip} /tmp/sm_ipc_main s 1 {i}"

        run_cmd(spines_ctrl_cmd, f"spines_ctrl_{i}", f"{log_dir}/out_spines_ctrl_{i}.txt")
        time.sleep(5)
        run_cmd(conf_agent_cmd, f"conf_agent_{i}", f"{log_dir}/out_conf_agent_{i}.txt")

    # Wait for (internal) spines process to exit (prevents script from exiting
    # and keeps container running, as long as spines is running)
    sp_proc.communicate()

if __name__ == "__main__":
    main(sys.argv[1:])
