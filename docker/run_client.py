import sys, argparse, subprocess, time, os

base_dir = "/app/spire"
log_dir = f"{base_dir}/logs"

def get_args(argv):
    parser = argparse.ArgumentParser(description="Run a Spire client")
    parser.add_argument('-ip', required=False, type=str, help='Client IP (default: 192.168.101.{100+ID})')
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
        i = 7 # assume plc/benchmark is always ID 7, default ip 192.168.101.107
        num_apps = 10
        ipc_path = "/tmp/rtu_ipc_main"
    elif args.type == 'hmi':
        i = 8 # assume hmi is always ID 8, default ip 192.168.101.108
        num_apps = 3
        ipc_path = "/tmp/hmi_ipc_main"
    else: #benchmark
        i = 7 # assume plc/benchmark is always ID 7, default ip 192.168.101.107
        num_apps = 10
        ipc_path = "/tmp/bm_ipc_main"
        args.type = "benchmark"

    if args.ip:
        ip = args.ip
    else:
        ip = f"192.168.101.{100 + i}"

    # Command definitions
    spines_ext_cmd = f"cd {base_dir}/spines/daemon && ./spines -p 8120 -c spines_ext.conf -I {ip}"

    # Launch external spines process
    sp_proc = run_cmd(spines_ext_cmd, f"spines_ext_{args.type}", f"{log_dir}/out_spines_ext_{args.type}.txt")

    # Conditionally launch proxy/plc processes
    if args.type == 'plc':
        pnnl_cmd = f"cd {base_dir}/plcs/pnnl_plc && ./openplc -m 502 -d 20000"
        run_cmd(pnnl_cmd, f"plc_pnnl", f"{log_dir}/out_plc_pnnl.txt")

        for jhu_id in range(10):
            jhu_cmd = f"cd {base_dir}/plcs/jhu{jhu_id} && ./openplc -m {503+jhu_id} -d {20001+jhu_id}"
            run_cmd(jhu_cmd, f"plc_jhu_{jhu_id}", f"{log_dir}/out_plc_jhu_{jhu_id}.txt")

        for ems_id in range(3):
            ems_cmd = f"cd {base_dir}/plcs/ems{ems_id} && ./openplc -m {513+ems_id} -d {20011+ems_id}"
            run_cmd(ems_cmd, f"plc_ems_{ems_id}", f"{log_dir}/out_plc_ems_{ems_id}.txt")
        for ems_id, name in enumerate(["ems_hydro", "ems_solar", "ems_wind"]):
            ems_cmd = f"cd {base_dir}/plcs/{name} && ./openplc -m {516+ems_id} -d {20014+ems_id}"
            run_cmd(ems_cmd, f"plc_{name}", f"{log_dir}/out_plc_{name}.txt")

        for proxy_id in range(17):
            proxy_cmd = f"cd {base_dir}/proxy && ./proxy {proxy_id} {ip}:8120 1"
            run_cmd(proxy_cmd, f"proxy_{proxy_id}", f"{log_dir}/out_proxy_{proxy_id}.txt")

    # Conditionally launch HMI processes
    if args.type == 'hmi':
        for hmi_id, name in enumerate(["jhu_hmi", "pnnl_hmi", "ems_hmi"]):
            hmi_cmd = f"cd hmis/{name} && ./{name} {ip}:8120 -port={5051+hmi_id}"
            run_cmd(hmi_cmd, f"{name}", f"{log_dir}/out_{name}.txt")

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
