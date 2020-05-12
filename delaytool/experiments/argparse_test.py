import argparse

parser = argparse.ArgumentParser()
parser.add_argument('input_files', type=str, nargs='+')
parser.add_argument('output_file', type=str)
args = parser.parse_args()
print(args.input_files, args.output_file)
