# .afdxxml format in AFDX Designer:
#   * VL path is specified as sequence of devices
#     and links in this path are unambiguous
#     because the format doesn't support multiple links between two switches
#
# .xml format in this work:
#  everything is same as in AFDX Designer format EXCEPT:
#  * VL path is specified as sequence of ports of devices
#      which are ingress ports towards this path
#  * all links must have the same capacity (rate)
#  * multiple links between two switches is supported
#
# modification of AFDX Designer format before converting to format of this work:
#   * if there must be M links (duplex) between two switches,
#     there are one link between them with attribute multi=M
#   * links without multi attribute or with multi=1 must have the same capacity (rate)
#
# pipeline used for data generation:
#   1. design network topology without multiple links between any two switches
#      and same rate of all links
#   2. manually add multi attributes with values which are >1 to links
#   3. rate of these links (multi-links) might be manually changed to affect
#      resulting VL configuration (it's recommended to multiply rate by factor in [1, M],
#      where M is value of multi attribute)
#   4. generate VL configuration by AFDX Designer
#   5. convert the result to this work format by this tool, which works this way:
#      5.1 duplicate links with multi=M M-1 times
#      5.2 set rates of all links are set to rate of non-multi links
#      5.3 add new switch ports corresponding to new links
#      5.4 change VL paths according to this work format:
#          in case of ambiguity choose random link between two switches
#          in such way that a VL is enters a switch by the only port
#   6. if resulting VL config is incorrect in terms of bandwidth usage,
#      try another random seed for step 5
#      (see usage of this tool by launching it without arguments)
#      or change multi-link rate differently on step 3
#      (if using rate of non-multi link, bandwidth usage is guaranteed
#      to be <= 100% in the whole resulting VL config)

def convertformat(filename_in, filename_out, seed=0):
    import xml.etree.ElementTree as etree
    from collections import defaultdict
    import random
    random.seed(seed)
    tree = etree.parse(filename_in)
    root = tree.getroot()
    resources = root.find('resources')

    # finding free numbers for new ports
    max_port_num = 0
    for device in resources.findall('endSystem') + resources.findall('switch'):
        local_max = max(map(int, device.get('ports').split(',')))
        if local_max > max_port_num:
            max_port_num = local_max

    # dict that returns switch number by port number
    port_switch = {}
    for sw in resources.findall('switch'):
        for port in map(int, sw.get('ports').split(',')):
            port_switch[port] = int(sw.get('number'))

    # getting rate of non-multi links
    rate = None
    for link in resources.findall('link'):
        m = link.get('multi')
        if m is None or int(m) <= 1:
            local_rate = int(link.get('capacity'))
            if rate is None:
                rate = local_rate
            elif rate != local_rate:
                raise Exception("capacity of all non-multi links must be the same")
    assert(rate is not None)

    # duplicating links and ports
    for link in resources.findall('link'):
        m = link.get('multi')
        if m is None:
            continue
        m = int(m)
        if m <= 1:
            continue
        link.set('capacity', str(rate))
        del link.attrib["multi"]
        ports = (int(link.get('from')), int(link.get('to')))
        sw_nums = tuple(port_switch.get(port) for port in ports)
        if sw_nums[0] is None or sw_nums[1] is None:
            raise Exception("multi link must connect two switches")
        switches = tuple(resources.find(f"switch[@number='{num}']") for num in sw_nums)
        print(f"link ({ports[0]}--{ports[1]}) divided into: ", end='')
        print(f"({ports[0]}--{ports[1]})", end='')
        for i in range(m - 1):
            for sw in switches:
                max_port_num += 1
                # adding new ports to switches
                sw.set("ports", sw.get("ports") + "," + str(max_port_num))
                port_switch[max_port_num] = int(sw.get('number'))
            link2 = etree.SubElement(resources, link.tag, link.attrib)
            link2.set('from', str(max_port_num - 1))
            link2.set('to', str(max_port_num))
            print(f", ({max_port_num - 1}--{max_port_num})", end="")
        print()

    # now resources specification is modified

    # dict that returns switch or end system number by port number
    port_device = port_switch.copy()
    for es in resources.findall('endSystem'):
        port_device[int(es.get('ports'))] = int(es.get('number'))

    # key is ordered device pair (d1, d2)
    # if (d1, d2) is connected by >=1 pairs of ports,
    # dict returns list of numbers of corresponding d2 ports
    # (these ports are ingress towards (d1, d2) direction)
    connection_port = defaultdict(list)
    for link in resources.findall('link'):
        ports = [int(link.get('from')), int(link.get('to'))]
        dev_nums = [port_device.get(port) for port in ports]
        for i in 0, 1:
            connection_port[(dev_nums[1-i], dev_nums[i])].append(ports[i])

    # modifying VL paths specifications: from devices to their ingress ports
    # with random choice in case of ambiguity:
    vls = root.find('virtualLinks')
    for vl in vls.findall('virtualLink'):
        # for each pair of connected devices in paths of this VL
        # the only link must be chosen from multiple choices
        # these pairs might repeat if VL has >1 path
        conn_chosen = {}
        for path_el in vl.findall('path'):
            path = list(map(int, path_el.get('path').split(',')))
            for pair in zip(path[:-1], path[1:]):
                conn_chosen[pair] = random.choice(connection_port[pair])
        for path_el in vl.findall('path'):
            path = list(map(int, path_el.get('path').split(',')))
            new_path = []
            for pair in zip(path[:-1], path[1:]):
                new_path.append(conn_chosen[pair])
            # print(vl.get('number'), ":", path_el.get('path'), end='')
            path_el.set('path', ",".join(map(str, new_path)))
            # print(" ->", path_el.get('path'))

    tree.write(filename_out)

def main(argv):
    if(len(argv) < 3):
        print(f"usage: {argv[0]} filename_in filename_out [random seed]")
        return
    seed = 0
    if len(argv) >= 4:
        seed = int(argv[3])
    convertformat(argv[1], argv[2], seed)

if __name__ == "__main__":
    import sys
    main(sys.argv)
