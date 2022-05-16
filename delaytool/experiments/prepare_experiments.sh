#!/bin/sh

# Python 3.6+ required
# before running this script:
# 1. delaytool must be built by running build.sh from its directory
# 2. AFDX Designer tool must be built, and the executable file in its "algo" directory must have path ./AFDX_Designer/algo/AFDX_DESIGN[.exe] (where current directory is "experiments"). AFDX Designer is from P.Vdovin dissertation which is referred to as "САПР AFDX" in my paper.

# designing virtual link configurations before launching delaytool upon them (launch_experiments.sh). VL configurations are designed by AFDX Designer tool by P.Vdovin.

# result of running of this script is already included in the release in vlconfigs directory

python generate_vls.py arch/2s-4e.afdxxml                     vlconfigs/msggen1 3 500 1000 1000000 10000 20000 20000 100000 1 3 123
python generate_vls.py arch/3s-48e-4L.afdxxml                 vlconfigs/msggen1 3 500 1000 1000000 10000 20000 20000 100000 1 3 123
python generate_vls.py arch/diamond-48e-4L.afdxxml            vlconfigs/msggen1 3 500 1000 1000000 10000 20000 20000 100000 1 3 123
python generate_vls.py arch/double_reserve_mod-48e-6L.afdxxml vlconfigs/msggen1 3 500 1000 1000000 10000 20000 20000 100000 1 3 123
python generate_vls.py arch/6s-54e-3L.afdxxml                 vlconfigs/msggen1 3 500 1000 1000000 10000 20000 20000 100000 1 3 123
python generate_vls.py arch/2s-4e.afdxxml                     vlconfigs/msggen2 3 500 1 4000 2 500 300 300 1 3 123
python generate_vls.py arch/3s-48e-4L.afdxxml                 vlconfigs/msggen2 3 500 1 4000 2 500 300 300 1 3 123
python generate_vls.py arch/diamond-48e-4L.afdxxml            vlconfigs/msggen2 3 500 1 4000 2 500 300 300 1 3 123
python generate_vls.py arch/double_reserve_mod-48e-6L.afdxxml vlconfigs/msggen2 3 500 1 4000 2 500 300 300 1 3 123
python generate_vls.py arch/6s-54e-3L.afdxxml                 vlconfigs/msggen2 3 500 1 4000 2 500 300 300 1 3 123
