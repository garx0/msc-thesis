#!/bin/sh

# Python 3.6+ required
# before running this script, delaytool must be built by running build.sh from its directory

# launching experiments with delaytool and writing aggregated output data to data/exp-oq-cioq.csv and data/exp-voq.csv. after running this script, Also, full XMLs containing all delays and jitters in all network configurations can be found in vlconfigs/msggen1/delays and vlconfigs/msggen2/delays (too large though, about 1 GB).

# input data required for these experiments is already included in the release in vlconfigs directory, but you can re-generate them by running prepare_experiments.sh

# csv files with main results of these experiments are already included in the release in data directory, but you can re-generate them by running this script

# These experiments are divided into two experiment series. Result of each series is written to separate csv file. Each series is a sequence of delaytool launches with different parameters. You can send SIGINT signal (Ctrl+C) to the process of experiments during any of these delaytool launches (for example, if calculation takes too long), and it will interrupt and stop only the current delaytool launch (and its duplicates), not the whole experiment series.

python experiments.py vlconfigs/msggen*/*_final_vls_*.xml data/exp.csv -s cioq oq --nfabrics 4 8 16 -j 0 -b 0.01 0.025 0.05 0.10 0.20 0.30 0.40 0.50 0.60 0.70 0.80 --iter 1
