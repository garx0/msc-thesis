0. The software implementation has been tested on Ubuntu 20.04.4 LTS, but is expected to work on Windows as well.

1. A project with a software implementation for a master's thesis is assembled by running the build.sh script. This requires CMake and g++.

2. After building the project, the executable files will be located at build/delaytool.

  - build/delaytool - main program for calculating delay estimates for network configuration in .xml format. Examples of such input data in .xml format are contained in the experiments/vlconfigs directory.

    * To run delaytool on data in the format used in AFDX CAD (with the .afdxxml extension), before launching this format will need to be converted to the format required for this software implementation using the experiments/convertformat.py program in Python 3.6+.

3. The results of the experiments are contained in experiments/data.

4. Prepared input data for experiments is contained in experiments/vlconfigs.

5. Experiments based on ready-made input data can be restarted using the experiments/launch_experiments.sh script, after building this project before.
  - This script requires Python 3.6+.
  - After the end of the experiments, more detailed raw results of experiments with all the delays and network configurations that were not originally attached to this project will appear in the directories in experiments/msggen1/delays and experiments/msggen2/delays.
  - Experiments can take few minutes, but they can be stopped by sending a SIGINT (Ctrl+C) signal to the experiment process, and then the data in the data directory will contain the results of not all experiments. If this signal is sent during one of the delaytool runs during the experiments, then this delaytool subprocess (experiment) will be stopped and canceled, but the series of experiments will continue with delaytool runs on the next files from the input data set.

6. The input data for the experiments can be regenerated using the script experiments/prepare_experiments.sh, but before running it, you need to assemble an instrumental system for constructing virtual channels in AFDX (AFDX CAD), developed as part of P. Vdovin's dissertation "Жадные алгоритмы и стратегии ограниченного перебора для планирования вычислений в системах с жесткими требованиями к качеству обслуживания" and not attached to this project. After building AFDX CAD, you need to copy the AFDX_DESIGN executable file from the algo folder of the AFDX CAD project to the experiments/AFDX_DESIGNER/algo folder of this project. The experiments/prepare_experiments.sh script also requires Python 3.6+.

7. Details about the operation of programs and scripts contained in the project can be found in the comments to Shell scripts and programs in Python and C++, as well as when running some programs without command line arguments.
