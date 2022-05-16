#!/usr/bin/python
import sys

def main(argv):
    import os, subprocess
    import random
    from flows_gen import gen_flows
    from convertformat import convertformat
    if(len(argv) < 13):
        print(
f"usage: python {argv[0]} arch output_dir n_tests n_msgs msg_size_min msg_size_max period_min period_max tmax_min tmax_max n_dests_min n_dests_max [random_seed]")
        return
    seed = 0
    if(len(argv) >= 14):
        seed = int(argv[13])
    (filename_arch, dir_name) = argv[1:3]
    (n_tests, n_msgs,
    msgSizeMin, msgSizeMax, periodMin, periodMax,
    tMaxMin, tMaxMax, n_dests_min, n_dests_max) = map(int, argv[3:13])

    if not os.path.exists(dir_name):
        os.makedirs(dir_name)

    random.seed(seed)
    n_succ = 0
    for i in range(1, n_tests+1):
        print()
        arch_name = os.path.splitext(os.path.split(filename_arch)[1])[0]
        filename_flows = f"{dir_name}/{arch_name}_flows_{i}.afdxxml"
        new_seed = random.randint(0, 1000000)
        gen_flows(filename_arch, filename_flows, n_msgs,
                  msgSizeMin, msgSizeMax, periodMin, periodMax,
                  tMaxMin, tMaxMax, n_dests_min, n_dests_max, new_seed)

        x_ext = ".exe" if sys.platform.startswith("win") else ""
        designer_path = "AFDX_Designer/algo/AFDX_DESIGN" + x_ext
        delaytool_path = "../build/delaytool" + x_ext

        if not os.path.isfile(designer_path):
            print(f"{designer_path} is not found, find and build AFDX Designer first")
            return

        if not os.path.isfile(delaytool_path):
            print(f"{delaytool_path} is not found, build delaytool first by running build.sh from its directory")
            return

        filename_vls = f"{dir_name}/{arch_name}_vls_{i}.afdxxml"
        if os.path.isfile(filename_vls):
            os.remove(filename_vls)
            assert(not os.path.isfile(filename_vls))
        command = f"{designer_path} {filename_flows} {filename_vls} a --limit-jitter=f"
        print(command)
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
        process.communicate()
        if not os.path.isfile(filename_vls):
            raise Exception("failed designing VL configuration")

        filename_xml = f"{dir_name}/{arch_name}_final_vls_{i}.xml"
        new_seed = random.randint(0, 1000000)
        convertformat(filename_vls, filename_xml, new_seed)

        n_succ += 1
    print(f"{n_succ}/{n_tests} vl configurations created successfully")

if __name__ == "__main__":
    main(sys.argv)
