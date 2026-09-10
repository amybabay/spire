import sys, argparse, subprocess

def get_args(argv):
    parser = argparse.ArgumentParser(description="Run N benchmark clients")
    parser.add_argument('-n', required=True, type=int)
    return parser.parse_args()

def main(argv):
    args = get_args(argv)

    n = args.n
    procs = []
    for i in range(1, n+1):
        bench_f = open("out_bench_{}.txt".format(i), 'a')
        bench_cmd = "cd benchmark; ./benchmark {id} {ip}:{spines_ext_port} 1000000 500".format(id=i, ip="192.168.101.107", spines_ext_port=8120)
        procs.append(subprocess.Popen(bench_cmd, stdout=bench_f, stderr=bench_f, shell=True))

    # Wait for procs to finish
    for p in procs:
        p.communicate()

if __name__ == "__main__":
    main(sys.argv[1:])
