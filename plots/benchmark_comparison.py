import os, time
from os.path import join, isdir
import numpy as np
import pandas as pd
from pandas.api.types import CategoricalDtype
import argparse
import re
from datetime import datetime, timezone
import json
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

# Apply rcParams FIRST, before creating any plots
plt.rcParams.update({
    'font.size': 14,
    'axes.titlesize': 16,
    'axes.labelsize': 14,
    'xtick.labelsize': 14,
    'ytick.labelsize': 14,
    'legend.fontsize': 14
})

'''
Script for constructing plots to compare different benchmarks. We choose a single system, and examine how it's behavior changes in a specific scenario, for different 
'''

VALID_SCENARIOS = ['baseline', 'skew', 'scalability', 'network', 'packet_loss', 'vary_hw', 'sunflower', 'server_skew', 'example']
VALID_WORKLOADS = ['tpcc', 'ycsb', 'pps', 'smallbank', 'movr', 'movie', 'dsh']
VALID_SYSTEMS = ['detock', 'slog', 'calvin', 'janus'] #, 'crdb'] # We will add in CRDB once we have the results
VALID_ENVIRONMENTS = ['local', 'st', 'aws']
SUPPORTED_VM_TYPES = ['t2.micro', 'm4.2xlarge', 'm5.2xlarge', 'r5.2xlarge', 'r5a.2xlarge', 'm5.4xlarge', 'r5.4xlarge', 'm8g.4xlarge', 'm6i.8xlarge']
# Better not include 'us-west_only' as it is a very special edge case
SUPPORTED_SERVER_SKEWS = ['balanced', 'us-west+', 'us-west+_eu-west+', 'us-west++', 'us-west++_eu-west++'] #, 'us-west_only']
# By default we use r5.4xlarge (following Detock's setup)
DEFAULT_VM_TYPE = 'r5.4xlarge'
LATENCY_PERCENTILES = [50, 95]

BENCHMARK_DISPLAY_NAME_MAP = {
    'ycsb': 'YCSB',
    'tpcc': 'TPC-C',
    'pps': 'PPS',
    'smallbank': 'SmallBank',
    'movr': 'MovR',
    'movie': 'DS Movie',
    'dsh': 'DS Hotels'
}
DESIRED_LABEL_ORDER = ['TPC-C', 'YCSB', 'PPS', 'SmallBank', 'MovR', 'DS Movie', 'DS Hotels']
SYSTEM_DISPLAY_NAME_MAP = {
    'detock': 'Detock',
    'slog': 'SLOG',
    'calvin': 'Calvin',
    'janus': 'Janus',
    'crdb': 'CRDB'
}

# Argument parser
parser = argparse.ArgumentParser(description="Extract experiment results and plot graph for a given scenario.")
parser.add_argument("-s",  "--scenario", default='baseline', choices=VALID_SCENARIOS, help="Type of experiment scenario to analyze (default: baseline)")
parser.add_argument("-sy", "--system", default='detock', choices=VALID_SYSTEMS, help="System to plot")
parser.add_argument("-e",  "--env", default='st', choices=VALID_ENVIRONMENTS, help="The environment where the experiment was run on")
parser.add_argument("-ll", "--log_latencies", default=True, help="Whether or not to plot the latency on a log scale.")
parser.add_argument("-sp", "--show_plot", default=True, help="Whether or not to display the plot.")

args = parser.parse_args()
scenario = args.scenario
system = args.system
env = args.env
log_latencies = args.log_latencies
show_plot = args.show_plot

show_legend = True
#if system != 'calvin':
#    show_legend = False

relevant_columns = ['x_var', f'{system}_throughput']
for p in LATENCY_PERCENTILES:
    relevant_columns.append(f'{system}_p{p}')

final_df = None

for workload in VALID_WORKLOADS:
    data_path = f'plots/data/{env}/{workload}/{scenario}.csv'
    if not os.path.exists(data_path):
        continue
    data = pd.read_csv(data_path)
    data.columns = data.columns.str.lower()
    # Select only the columns we need for this specific system
    if scenario == 'scalability':
        cols_to_extract = [f'{system}_input_throughput', f'{system}_throughput'] + [f'{system}_p{p}' for p in LATENCY_PERCENTILES]
    else:
        cols_to_extract = ['x_var', f'{system}_throughput'] + [f'{system}_p{p}' for p in LATENCY_PERCENTILES]
    # Ensure columns exist in the CSV to avoid crashing
    available_cols = [c for c in cols_to_extract if c in data.columns]
    workload_data = data[available_cols].copy()
    # Rename columns to include the workload name (e.g., 'ycsb_throughput')
    rename_map = {
        f'{system}_input_throughput': 'x_var',
        f'{system}_throughput': f'{workload}_throughput',
        f'{system}_p{LATENCY_PERCENTILES[0]}': f'{workload}_p{LATENCY_PERCENTILES[0]}',
        f'{system}_p{LATENCY_PERCENTILES[1]}': f'{workload}_p{LATENCY_PERCENTILES[1]}'
    }
    workload_data.rename(columns=rename_map, inplace=True)
    # Merge into the final dataframe
    if final_df is None:
        final_df = workload_data
    else:
        # 'how=outer' handles the mismatch in x_var values
        final_df = pd.merge(final_df, workload_data, on='x_var', how='outer')

final_df = final_df.sort_values('x_var').reset_index(drop=True)

# Plot the final data
fig_len = 4
if show_legend:
    fig_len += 0.25
fig, axes = plt.subplots(1, 2, figsize=(8, fig_len), sharex=True)
for workload in VALID_WORKLOADS:
    tp_col = f'{workload}_throughput'
    p50_col = f'{workload}_p{LATENCY_PERCENTILES[0]}'
    p99_col = f'{workload}_p{LATENCY_PERCENTILES[1]}'
    
    if tp_col not in final_df.columns:
        continue

    # FIX: Filter NaNs for each specific workload so lines connect
    # We create a mask for throughput and for latency
    mask_tp = final_df[tp_col].notnull()
    mask_lat = final_df[p50_col].notnull() # assuming p50 and p95/p99 share the same x-points

    if sum(mask_tp) == 0: # This happens if we only have data for some benchamrks so far
        continue

    new_x_vars = final_df.loc[mask_tp, 'x_var']
    if scenario == 'baseline':
        if workload == 'tpcc':
            #             0.00;0.01;0.02;0.05;0.075;0.10;0.15;0.20;0.25;0.30;0.35;0.40;0.45;0.50;0.55;0.60;0.65;0.70;0.75;0.80,0.85;0.90;0.95;1.00]
            #new_x_vars = [0,   4.66,8.96,19.9,27.1, 33.0,42.0,48.0,52.4,56.0,58.7,61.4,63.8,66.1,68.3,70.4,72.5,74.6,76.8,79.1,81.4,83.6,85.8,88.0]
            included_tpcc_xaxis_points = [0.0,  0.01, 0.02, 0.05, 0.075, 0.10, 0.15, 0.20, 0.25, 0.30, 0.40, 0.50, 0.60, 0.70, 0.80, 0.90, 1.00]
            new_x_vars =                 [0,    4.66, 8.96, 19.9, 27.1,  33.0, 42.0, 48.0, 52.4, 56.0, 61.4, 66.1, 70.4, 74.6, 79.1, 83.6, 88.0]
            included_rows = [True if x in included_tpcc_xaxis_points else False for x in final_df['x_var']]
        elif workload == 'dsh':
            new_x_vars = (1 / 100) * 100 * final_df.loc[mask_tp, 'x_var']
        elif workload == 'pps':
            new_x_vars = (58 / 100) * final_df.loc[mask_tp, 'x_var']
        elif workload == 'smallbank':
            new_x_vars = (20 / 100) * final_df.loc[mask_tp, 'x_var']
        elif workload == 'movr':
            new_x_vars = (71 / 100) * final_df.loc[mask_tp, 'x_var']
    elif scenario == 'sunflower':
        new_x_vars = 24 * final_df[final_df[tp_col].notnull()]['x_var'] / max(final_df[final_df[tp_col].notnull()]['x_var'])

    # Plot Throughput
    # Plot Latency (p50 as line, p95/p99 as second line)
    # We use the same color for p50 and p95/p99 to keep the plot readable
    if workload == 'tpcc' and scenario == 'baseline':
        line, = axes[0].plot(new_x_vars, final_df.loc[included_rows, tp_col], label=BENCHMARK_DISPLAY_NAME_MAP[workload])
        color = line.get_color()
        axes[1].plot(new_x_vars, final_df.loc[included_rows, p50_col])
        axes[1].plot(new_x_vars, final_df.loc[included_rows, p99_col], linestyle='--', color=color, alpha=0.7)
    else:
        line, = axes[0].plot(new_x_vars, final_df.loc[mask_tp, tp_col], label=BENCHMARK_DISPLAY_NAME_MAP[workload])
        color = line.get_color()
        axes[1].plot(new_x_vars, final_df.loc[mask_lat, p50_col])
        axes[1].plot(new_x_vars, final_df.loc[mask_lat, p99_col], linestyle='--', color=color, alpha=0.7)
    
# Formatting
axes[0].set_title(f'Throughput ({SYSTEM_DISPLAY_NAME_MAP[system]})')
axes[0].set_ylabel('Throughput (txns/s)')
axes[1].set_title(f'Latency ({SYSTEM_DISPLAY_NAME_MAP[system]})')
axes[1].set_ylabel('Latency (ms)')
if scenario == 'baseline':
    axes[0].set_xlabel('Geo-distribution (%)')
    axes[1].set_xlabel('Geo-distribution (%)')
elif scenario == 'skew':
    axes[0].set_xlabel('Skew factor')
    axes[1].set_xlabel('Input throughput (txns/s)')
elif scenario == 'scalability':
    axes[0].set_xlabel('Input throughput (txns/s)')
    axes[1].set_xlabel('Input throughput (txns/s)')
    axes[0].set_xlim(-10_000, 250_000)
    axes[1].set_xlim(-10_000, 250_000)
elif scenario == 'network':
    axes[0].set_xlabel('Extra delay (ms)')
    axes[1].set_xlabel('Extra delay (ms)')
elif scenario == 'packet_loss':
    axes[0].set_xlabel('Packet loss (%)')
    axes[1].set_xlabel('Packet loss (%)')
elif scenario == 'sunflower':
    axes[0].set_xlabel('Time (h)')
    axes[1].set_xlabel('Time (h)')

handles, labels = axes[0].get_legend_handles_labels()
labels = [l[:1].capitalize()+l[1:] for l in labels]
handle_map = dict(zip(labels, handles))
# 4. Rebuild handles and labels based on desired_order
# We use a list comprehension to ensure we only include labels that actually exist in the plot
sorted_handles = [handle_map[l] for l in DESIRED_LABEL_ORDER if l in handle_map]
sorted_labels = [l for l in DESIRED_LABEL_ORDER if l in handle_map]

if show_legend:
    fig.legend(sorted_handles, sorted_labels, loc='upper center', ncol=round(len(VALID_WORKLOADS)/2), bbox_to_anchor=(0.5, 1.1))
if log_latencies:
    axes[1].set_yscale('log')
    axes[1].set_ylim(bottom=1)
axes[0].set_ylim(bottom=0)
axes[0].yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, pos: f'{x/1000:.0f}k'))
if scenario == 'scalability':
    axes[0].xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, pos: f'{x/1000:.0f}k'))
    axes[1].xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, pos: f'{x/1000:.0f}k'))
axes[0].grid(which='major', axis='y')
axes[1].grid(which='major', axis='y')

plt.tight_layout(rect=[0, 0, 1, 0.95])
# Save figures
output_path = f'plots/output/{env}/{scenario}/{system}_{scenario}'
png_path = output_path + '.png'
pdf_path = output_path + '.pdf'
os.makedirs('/'.join(output_path.split('/')[:-1]), exist_ok=True)
plt.savefig(png_path, dpi=300, bbox_inches='tight', pad_inches=0.0)
plt.savefig(pdf_path, bbox_inches='tight', pad_inches=0.0)
if show_plot:
    matplotlib.use('Agg')
    plt.show()

print("Done")
