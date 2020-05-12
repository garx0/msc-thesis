import os, sys, subprocess
import random

def gen_one_df(fromPartition, toPartitions, jMax, msgSize, period, tMax, id):
    dest = ""
    for part in toPartitions:
        dest += str(part) + ','
    dest = dest[:-1]
    ans = '\t\t<dataFlow dest="' + dest + '" id="Data Flow ' + str(id) + '" jMax="' + str(jMax) + '" msgSize="' + str(msgSize) + '" period="' + str(period)
    ans += '" source="' + str(fromPartition) + '" tMax="' + str(tMax) + '" vl="None"/>'

    return ans

def get_part_groups(filename):
    import xml.etree.ElementTree as etree
    from collections import defaultdict
    tree = etree.parse(filename)
    root = tree.getroot()
    groups_dict = defaultdict(list)
    for part in root.find('resources').findall('partition'):
        groups_dict[int(part.get('connectedTo'))].append(part.get('number'))
    return list(groups_dict.values())

def gen_source(part_groups):
    group = random.randint(0, len(part_groups) - 1)
    id = random.randint(0, len(part_groups[group]) - 1)
    return {'id': part_groups[group][id], 'group': group}

def gen_dests(sourceGroup, destNumber, partGroups):
    dests = []
    for i in range(destNumber):
        id = -1
        while id == -1 or (id in dests):
            group = sourceGroup

            while group == sourceGroup:
                group = random.randint(0, len(partGroups) - 1)
            index = random.randint(0, len(partGroups[group]) - 1)
            id = partGroups[group][index]

        dests.append(id)
    return dests

def gen_random_df(id, part_groups, msgSizeMin, msgSizeMax, periodMin, periodMax,
                       tMaxMin, tMaxMax, n_dests_min, n_dests_max):
    msgSize = random.randint(msgSizeMin, msgSizeMax)
    period = random.randint(periodMin, periodMax)
    tMax = random.randint(tMaxMin, tMaxMax)
    destNumber = random.randint(n_dests_min, n_dests_max)
    sourceInfo = gen_source(part_groups)
    dests = gen_dests(sourceInfo['group'], destNumber, part_groups)
    return gen_one_df(sourceInfo['id'], dests, 0, msgSize, period, tMax, id)

def gen_flows(filename_in, filename_out, n_msgs,
              msgSizeMin, msgSizeMax, periodMin, periodMax,
              tMaxMin, tMaxMax, n_dests_min, n_dests_max, seed):
    random.seed(seed)
    part_groups = get_part_groups(filename_in)
    with open(filename_in, 'r') as f:
        textArr = f.readlines()
    text = "".join(textArr[:-2])
    for id in range(1, n_msgs + 1):
        text += gen_random_df(
            id, part_groups, msgSizeMin, msgSizeMax, periodMin, periodMax,
            tMaxMin, tMaxMax, n_dests_min, n_dests_max
        ) + os.linesep
    text += "\t</dataFlows>" + os.linesep
    text += "</afdxxml>" + os.linesep
    with open(filename_out, 'w') as f:
        f.write(text)
