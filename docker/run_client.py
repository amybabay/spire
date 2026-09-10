import sys, argparse, subprocess, time

def get_args(argv):
    parser = argparse.ArgumentParser(description="Run a Spire client")
    parser.add_argument('-id', required=True, type=int)
    return parser.parse_args()

def main(argv):
    args = get_args(argv)

    i = args.id
    spines_ext_f  = open("out_spines_ext_client{}.txt".format(i), 'a')
    spines_ctrl_f = open("out_spines_ctrl_client{}.txt".format(i), 'a')
    conf_agent_f  = open("out_conf_agent_client{}.txt".format(i), 'a')

    spines_ext_cmd  = "cd spines/daemon; ./spines -p 8120 -c spines_ext.conf -I 192.168.101.10{}".format(i)
    spines_ctrl_cmd = "cd spines/daemon; ./spines -p 8900 -c spines_ctrl.conf"
    if i == 7:
        num_apps = 10
        #conf_agent_cmd  = "cd prime/bin; ./config_agent {} 192.168.101.10{} /tmp/rtu_ipc_main p {}".format(i,i,num_apps)
        conf_agent_cmd  = "cd prime/bin; ./config_agent {} 192.168.101.10{} /tmp/bm_ipc_main p {}".format(i,i,num_apps)
    else:
        num_apps = 3
        conf_agent_cmd  = "cd prime/bin; ./config_agent {} 192.168.101.10{} /tmp/hmi_ipc_main p {}".format(i,i,num_apps)

    sp_proc = subprocess.Popen(spines_ext_cmd, stdout=spines_ext_f, stderr=spines_ext_f, shell=True)
    subprocess.Popen(spines_ctrl_cmd, stdout=spines_ctrl_f, stderr=spines_ctrl_f, shell=True)
    time.sleep(5)
    subprocess.Popen(conf_agent_cmd, stdout=conf_agent_f, stderr=conf_agent_f, shell=True)

    # Wait for spines process to exit (prevents script from exiting and keeps
    # container running, as long as spines is running)
    sp_proc.communicate()

if __name__ == "__main__":
    main(sys.argv[1:])
