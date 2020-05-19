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
        delaytool_path = "../cmake-build-debug/delaytool" + x_ext
        deletepaths_path = "../cmake-build-debug/deletepaths" + x_ext

        filename_vls = f"{dir_name}/{arch_name}_vls_{i}.afdxxml"
        command = f"{designer_path} {filename_flows} {filename_vls} a --limit-jitter=f"
        print(command)
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
        process.communicate()

        filename_xml = f"{dir_name}/{arch_name}_vls_{i}.xml"
        new_seed = random.randint(0, 1000000)
        convertformat(filename_vls, filename_xml, new_seed)

        filename_xml_fix = f"{dir_name}/{arch_name}_final_vls_{i}.xml"
        new_seed = random.randint(0, 1000000)
        command = f"{deletepaths_path} {filename_xml} {filename_xml_fix} -r -s {new_seed}"
        print(command)
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
        process.communicate()
        if not os.path.isfile(filename_xml_fix) or os.path.getsize(filename_xml_fix) == 0:
            print("error: deletepaths didn't work, try changing seed or other parameters")
            continue

        filename_temp = f"{dir_name}/temp.xml"
        command = f"{delaytool_path} {filename_xml_fix} {filename_temp} -s mock"
        print(command)
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
        for line in process.communicate():
            print(line.decode('utf-8'), end='')

        if not os.path.isfile(filename_temp) or os.path.getsize(filename_temp) == 0:
            print("error: delay calculation is not ready, try changing seed or other parameters")
            split = os.path.split(filename_xml_fix)
            new_name = os.path.join(split[0], split[1].replace('final', 'fail'))
            os.rename(filename_xml_fix, new_name)
        else:
            n_succ += 1
    if os.path.isfile(filename_temp):
        os.remove(filename_temp)
    print(f"{n_succ}/{n_tests} vl configurations created successfully")

if __name__ == "__main__":
    main(sys.argv)
