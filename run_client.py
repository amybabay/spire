import sys, argparse, subprocess

def get_args(argv):
    parser = argparse.ArgumentParser(description="Run a Spire client")
    #parser.add_argument('-id', required=True, type=int)
    return parser.parse_args()

def main(argv):
    #args = get_args(argv)

    #i = args.id
    # Output to file
    # spines_ext_f = open("out_spines_ext_{}.txt".format("client"), 'a')

    # Output to docker logs
    spines_ext_f = sys.stdout

    spines_ext_cmd = "cd spines/daemon; ./spines -p 8120 -c spines_ext.conf -I 192.168.101.10{}".format(7)

    sp_proc = subprocess.Popen(spines_ext_cmd, stdout=spines_ext_f, stderr=spines_ext_f, shell=True)

    # Wait for spines process to exit (prevents script from exiting and keeps
    # container running, as long as spines is running)
    sp_proc.communicate()

if __name__ == "__main__":
    main(sys.argv[1:])
