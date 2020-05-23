#!/bin/sh

# Python 3.6+ required
# before running this script, delaytool must be built by running build.sh from its directory

# launching experiments with delaytool and writing aggregated output data to data/exp-oq-cioq.csv and data/exp-voq.csv. after running this script, Also, full XMLs containing all delays and jitters in all network configurations can be found in vlconfigs/msggen1/delays and vlconfigs/msggen2/delays (too large though, about 1 GB).

# input data required for these experiments is already included in the release in vlconfigs directory, but you can re-generate them by running prepare_experiments.sh

# csv files with main results of these experiments are already included in the release in data directory, but you can re-generate them by running this script

# These experiments are divided into two experiment series. Result of each series is written to separate csv file. Each series is a sequence of delaytool launches with different parameters. You can send SIGINT signal (Ctrl+C) to the process of experiments during any of these delaytool launches (for example, if calculation takes too long), and it will interrupt and stop only the current delaytool launch (and its duplicates), not the whole experiment series.

# all the experiments took several hours on my computer

python experiments.py vlconfigs/msggen*/*_final_vls_*.xml data/exp-voq.csv -s voqa voqb oqa -c 256 -d 16 32 64 128 256 -j 0 -b 0.01 0.02 0.03 0.04 0.05 0.06 0.07 0.08 0.09 0.1 0.11 0.12 0.13 0.14 0.15 0.16 0.17 0.18 0.19 0.2 0.21 0.22 0.23 0.24 0.25 0.26 0.27 0.28 0.29 0.3 0.31 0.32 0.33 0.34 0.35 0.36 0.37 0.38 0.39 0.4 -i 1
python experiments.py vlconfigs/msggen*/*_final_vls_*.xml data/exp-oq-cioq.csv -s oqp oqa oqb -c 256 -d 16 32 64 128 -j 0 -b 0.01 0.025 0.05 0.10 0.15 0.20 0.25 0.30 0.35 0.40 0.45 0.50 0.55 0.60 0.65 0.70 0.75 0.80 0.85 0.90 0.95 0.97 0.98 0.99 0.995 0.9975 0.99875


