import sys, argparse, subprocess

def get_args(argv):
    parser = argparse.ArgumentParser(description="Run a Spire replica with given ID")
    parser.add_argument('-id', required=True, type=int)
    return parser.parse_args()

def main(argv):
    args = get_args(argv)

    i = args.id
    spines_int_f = open("out_spines_int_{}.txt".format(i), 'a')
    spines_ext_f = open("out_spines_ext_{}.txt".format(i), 'a')
    sm_f         = open("out_sm_{}.txt".format(i), 'a')
    prime_f      = open("out_prime_{}.txt".format(i), 'a')

    spines_int_cmd = "cd spines/daemon; ./spines -p 8100 -c spines_int.conf -I 192.168.101.10{}".format(i)
    spines_ext_cmd = "cd spines/daemon; ./spines -p 8120 -c spines_ext.conf -I 192.168.101.10{}".format(i)
    sm_cmd         = "cd scada_master; ./scada_master {} {} 192.168.101.10{}:8100 192.168.101.10{}:8120".format(i,i,i,i)
    prime_cmd      = "cd prime/bin; ./prime -i {} -g {}".format(i,i)

    sp_proc = subprocess.Popen(spines_int_cmd, stdout=spines_int_f, stderr=spines_int_f, shell=True)
    subprocess.Popen(spines_ext_cmd, stdout=spines_ext_f, stderr=spines_ext_f, shell=True)
    subprocess.Popen(sm_cmd, stdout=sm_f, stderr=sm_f, shell=True)
    subprocess.Popen(prime_cmd, stdout=prime_f, stderr=prime_f, shell=True)

    # Wait for spines process to exit (prevents script from exiting and keeps
    # container running, as long as spines is running)
    sp_proc.communicate()

if __name__ == "__main__":
    main(sys.argv[1:])