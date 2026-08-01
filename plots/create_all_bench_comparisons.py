import os
import argparse

VALID_SCENARIOS = ['baseline', 'skew', 'scalability', 'network', 'packet_loss', 'sunflower']
VALID_SYSTEMS = ['detock', 'slog', 'calvin', 'janus'] #, 'crdb'] # We will add in CRDB once we have the results
VALID_ENVIRONMENTS = ['local', 'st', 'aws']

# Argument parser
parser = argparse.ArgumentParser(description="Extract experiment results and plot graph for a given scenario.")
parser.add_argument("-e",  "--env", default='st', choices=VALID_ENVIRONMENTS, help="The environment where the experiment was run on")
parser.add_argument("-ll", "--log_latencies", default=True, action='store_true', help="Whether or not to plot the latency on a log scale.")

args = parser.parse_args()
env = args.env
log_latencies = args.log_latencies

for scenario in VALID_SCENARIOS:
    print(f"Generating benchmark comparison plots for scenario: {scenario}")
    for system in VALID_SYSTEMS:
        os.system(f"python3.8 plots/benchmark_comparison.py -s {scenario} -sy {system} -e {env} -ll {log_latencies} -sp False")

print("All benchmark comparisons have been generated and saved to the plots directory.")
