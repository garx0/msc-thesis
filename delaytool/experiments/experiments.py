import os, sys, subprocess

x_ext = ".exe" if sys.platform.startswith("win") else ""
delaytool_path = "../cmake-build-debug/delaytool" + x_ext

def get_bw_stats(filename_in):
    filename_out = "temp.xml"
    command = f"{delaytool_path} {filename_in} {filename_out} -s mock"
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
    bw_min, bw_max, bw_mean, bw_var = 0, 0, 0, 0
    for line_enc in process.communicate():
        lines = line_enc.decode('utf-8')
        for line in lines.split('\n'):
            if line.startswith('bwUsage'):
                tokens = line.replace('=', ',').split(',')
                numbers = []
                for token in tokens:
                    try:
                        num = float(token)
                        numbers.append(num)
                    except ValueError:
                        pass
                if(len(numbers) >= 4):
                    bw_min, bw_max, bw_mean, bw_var = numbers[:4]
                    break
    if os.path.isfile(filename_out):
        os.remove(filename_out)
    return bw_min, bw_max, bw_mean, bw_var


def get_rate(filename_in):
    import xml.etree.ElementTree as etree
    tree = etree.parse(filename_in)
    root = tree.getroot()
    return int(root.find('resources').find('link').get('capacity'))

# avg_bw is expected average bandwidth
# voq_dur is duration of voq processing period in ms
# returns calculation time in seconds (float)
def calc_delays(filename_in, filename_out, bw_stats, rate, scheme, avg_bw, cell_size=None, voq_dur=None):
    from math import ceil, inf
    from datetime import datetime
    bw_min, bw_max, bw_mean, bw_var = bw_stats
    new_rate = ceil(rate * bw_mean / avg_bw)
    new_bw_max = bw_max / bw_mean * avg_bw
    if new_bw_max > 1:
        print(f"max bandwidth will be: {new_bw_max} > 1, try smaller avg bandwidth")
        return 0, 'bw overload'
    command = f"{delaytool_path} {filename_in} {filename_out} -s {scheme} -r {new_rate} --jitdef 0"
    if scheme in ("oqa", "oqb", "voqa", "voqb"):
        if cell_size is None:
            print("error, specify cell size")
            return 0, 'py_arg'
        command += f" -c {cell_size}"
    if scheme in ("voqa", "voqb"):
        if voq_dur is None:
            print("error, specify voq processing period duration in ms")
            return 0, 'py_arg'
        voq_l = ceil(voq_dur * new_rate / cell_size)
        command += f" -p {voq_l}"
    print(command)
    t1 = datetime.now()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
    err_msg = None
    err = None
    try:
        for line_enc in process.communicate():
            lines = line_enc.decode('utf-8')
            print(lines)
            for line in lines.split('\n'):
                if line.lower().startswith("error"):
                    err_msg = line
                    err = 'other'
    except KeyboardInterrupt:
        err_msg = 'cancelled this command execution'
        err = 'cancel'
    t2 = datetime.now()
    if err_msg is not None:
        print(err_msg)
        return 0, err
    return (t2-t1).total_seconds(), err

def delay_stats(filename_in):
    import xml.etree.ElementTree as etree
    tree = etree.parse(filename_in)
    root = tree.getroot()
    vls = root.find('virtualLinks')
    delays = []
    jitters = []
    for vl in vls.findall('virtualLink'):
        paths_delay = []
        paths_jit = []
        for path_el in vl.findall('path'):
            if path_el.get('maxDelay') is None:
                return 0, 0, 0, 0
            paths_delay.append(int(path_el.get('maxDelay')))
            paths_jit.append(int(path_el.get('maxJit')))
        delays.append(sum(paths_delay) / len(paths_delay))
        jitters.append(sum(paths_jit) / len(paths_jit))
    delays_max = max(delays)
    jitters_max = max(jitters)
    delays_mean = sum(delays) / len(delays)
    jitters_mean = sum(jitters) / len(jitters)
    return delays_max, jitters_max, delays_mean, jitters_mean

# split into directory, name and extension
def fullsplit(path):
    import os
    splitext = os.path.splitext(path)
    return (*os.path.split(splitext[0]), splitext[1])

# experimemts on one file with VL config : filename_in
# file_out is file object, not filename (csv table).
# function writes lines to the file without writing header.
def exp_config(filename_in, file_out, scheme_list, cell_size_list, voq_dur_list,
    avg_bw_list, n_iter, fixed_cell_time=False):
    from math import ceil
    split = fullsplit(filename_in)
    delays_dir = os.path.join(split[0], "delays")
    if not os.path.exists(delays_dir):
        os.makedirs(delays_dir)
    bw_stats = get_bw_stats(filename_in)
    rate = get_rate(filename_in)
    for scheme in scheme_list:
        cs_list_end = 1
        vd_list_end = 1
        is_cell = scheme in ("oqa", "oqb", "voqa", "voqb")
        is_voq = scheme in ("voqa", "voqb")
        if is_cell:
            cs_list_end = None
        if is_voq:
            vd_list_end = None
        for cell_size in cell_size_list[:cs_list_end]:
            for voq_dur in voq_dur_list[:vd_list_end]:
                fct_stop = False
                for avg_bw in avg_bw_list:
                    cell_size_mod = cell_size
                    filename_config = f"{os.path.join(delays_dir, split[1])}_delays_{scheme}"
                    if is_cell:
                        if fixed_cell_time:
                            cell_size_mod = ceil(cell_size * bw_stats[2] / avg_bw)
                            if cell_size_mod == 1:
                                fct_stop == True
                        filename_config += f"_c={cell_size_mod}"
                    if is_voq:
                        filename_config += f"_lt={voq_dur}"
                    filename_config += f"_bwavg={avg_bw}{split[2]}"
                    avg_time = 0
                    for it in range(n_iter):
                        t, err = calc_delays(filename_in, filename_config, bw_stats, rate,
                            scheme, avg_bw, cell_size_mod, voq_dur)
                        if err is not None:
                            avg_time = 0
                            break
                        avg_time += t
                    avg_time /= n_iter
                    if avg_time != 0:
                        delays_max, jitters_max, delays_mean, jitters_mean = delay_stats(filename_config)
                    else:
                        delays_max, jitters_max, delays_mean, jitters_mean = 0, 0, 0, 0
                    config_name = split[1]
                    if err is None:
                        err = ""
                    file_out.write(f'"{config_name}",{scheme},{cell_size_mod},{voq_dur},{avg_bw},'
                        + f'{delays_max},{delays_mean},{jitters_max},{jitters_mean},{avg_time},{rate},{cell_size},'
                        + f'{bw_stats[0]},{bw_stats[1]},{bw_stats[2]},{bw_stats[3]},{fixed_cell_time},"{err}"\n')
                    if fct_stop:
                        break # from avg_bw cycle


# example:
# python experiments.py file1 file2 file3 file0
#     -s oqp voqa voqb oqa oqb -c 53 -d 128 256 -b 0.01 0.05 0.10 0.15 0.20 0.25 0.3 0.4 0.5 0.75
def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('input_file', type=str, nargs='+')
    parser.add_argument('output_file', type=str)
    parser.add_argument('-s', '--scheme', type=str, nargs='+', dest='scheme',
        choices=["oqp", "voqa", "voqb", "oqa", "oqb", "mock"])
    parser.add_argument('-c', '--cellsize', type=int, nargs='+', dest='cell_size')
    parser.add_argument('-d', '--voqdur', type=float, nargs='+', dest='voq_dur',
        help="voq processing period duration in ms")
    parser.add_argument('-b', '--bw', type=float, nargs='+', dest='avg_bw',
        help="average bandwidth usage in [0,1]")
    parser.add_argument('-i', '--iter', type=int, dest='n_iter', default=3,
        help="number of launches of one command to count average calculating time")
    parser.add_argument('-f', '--fct', action='store_true', dest='fct',
        help="fixed cell time: cell size is modified with bw usage to keep cell transfer duration fixed")

    args = parser.parse_args()
    filenames_in = args.input_file
    print(filenames_in)
    filename_out = args.output_file
    print(filename_out)
    scheme_list = args.scheme
    cell_size_list = args.cell_size
    voq_dur_list = args.voq_dur
    avg_bw_list = args.avg_bw
    n_iter = args.n_iter
    fct = args.fct

    with open(filename_out, "w") as file_out:
        # csv header
        file_out.write(f'config,scheme,cell_size,voq_dur,avg_bw,'
            + f'delays_max,delays_mean,jitters_max,jitters_mean,time,rate_orig,cell_size_orig,'
            + f'bw_min_orig,bw_max_orig,bw_mean_orig,bw_var_orig,fixed_cell_time,error\n')
        for filename_in in filenames_in:
            exp_config(filename_in, file_out, scheme_list, cell_size_list,
                voq_dur_list, avg_bw_list, n_iter, fixed_cell_time=fct)

if __name__ == "__main__":
    main()
