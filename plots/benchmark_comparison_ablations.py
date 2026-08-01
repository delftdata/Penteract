import os
import pandas as pd
import argparse
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

plt.rcParams.update({
    'font.size': 14,
    'axes.titlesize': 16,
    'axes.labelsize': 14,
    'xtick.labelsize': 14,
    'ytick.labelsize': 14,
    'legend.fontsize': 14
})

VALID_SCENARIOS = ['baseline', 'skew', 'scalability', 'network', 'packet_loss', 'vary_hw', 'sunflower', 'server_skew', 'example']
VALID_ENVIRONMENTS = ['local', 'st', 'aws']
VALID_WORKLOADS = ['smallbank', 'movie'] #['tpcc', 'ycsb', 'pps', 'smallbank', 'movr', 'movie', 'dsh']
SYSTEMS = ['detock', 'calvin']
LATENCY_PERCENTILES = [50, 95]

BENCHMARK_DISPLAY_NAME_MAP = {
    'tpcc': 'TPC-C',
    'ycsb': 'YCSB',
    'pps': 'PPS',
    'smallbank': 'SmallBank',
    'movr': 'MovR',
    'movie': 'DS Movie',
    'dsh': 'DS Hotels'
}

# Define workload colors:
WORKLOAD_COLORS = {
    'tpcc': plt.cm.tab10(0),        # 1st color
    'ycsb': plt.cm.tab10(1),        # 2nd color
    'pps': plt.cm.tab10(2),         # 3rd color
    'smallbank': plt.cm.tab10(3),   # 4th color
    'movr': plt.cm.tab10(4),        # 5th color
    'movie': plt.cm.tab10(5),       # 6th color
    'dsh': plt.cm.tab10(6)          # 7th color
}

# Define latency ablation colors (LSH and MH)
LATENCY_ABLATION_COLORS = {
    'lsh': plt.cm.tab10(0),   # 1st color (blue)
    'mh': plt.cm.tab10(1)     # 2nd color (orange)
}

parser = argparse.ArgumentParser(description="Plot throughput comparison for Detock and Calvin.")
parser.add_argument("-s",  "--scenario", default='baseline', choices=VALID_SCENARIOS)
parser.add_argument("-e",  "--env", default='st', choices=VALID_ENVIRONMENTS)
parser.add_argument("-sp", "--show_plot", default=True, action='store_true')
args = parser.parse_args()

scenario = args.scenario
env = args.env
show_plot = args.show_plot

final_df = None

for workload in VALID_WORKLOADS:
    data_path = f'plots/data/{env}/{workload}/{scenario}.csv'
    if not os.path.exists(data_path):
        continue

    data = pd.read_csv(data_path)
    data.columns = data.columns.str.lower()

    if scenario == 'scalability':
        cols = ['x_var'] + [f'{sys}_throughput' for sys in SYSTEMS] + \
               [f'detock_lsh_p{p}' for p in LATENCY_PERCENTILES] + \
               [f'detock_fsh_p{p}' for p in LATENCY_PERCENTILES] + \
               [f'detock_mh_p{p}' for p in LATENCY_PERCENTILES] + \
               [f'calvin_p{p}' for p in LATENCY_PERCENTILES]
    else:
        cols = ['x_var'] + [f'{sys}_throughput' for sys in SYSTEMS] + \
               [f'detock_lsh_p{p}' for p in LATENCY_PERCENTILES] + \
               [f'detock_fsh_p{p}' for p in LATENCY_PERCENTILES] + \
               [f'detock_mh_p{p}' for p in LATENCY_PERCENTILES] + \
               [f'calvin_p{p}' for p in LATENCY_PERCENTILES]

    available_cols = [c for c in cols if c in data.columns]
    if 'x_var' not in available_cols:
        continue

    workload_data = data[available_cols].copy()
    rename_map = {
        'x_var': 'x_var',
        'detock_throughput': f'{workload}_detock',
        'calvin_throughput': f'{workload}_calvin'
    }
    # Add latency column renames
    for p in LATENCY_PERCENTILES:
        if f'detock_lsh_p{p}' in available_cols:
            rename_map[f'detock_lsh_p{p}'] = f'{workload}_detock_lsh_p{p}'
        if f'detock_fsh_p{p}' in available_cols:
            rename_map[f'detock_fsh_p{p}'] = f'{workload}_detock_fsh_p{p}'
        if f'detock_mh_p{p}' in available_cols:
            rename_map[f'detock_mh_p{p}'] = f'{workload}_detock_mh_p{p}'
        if f'calvin_p{p}' in available_cols:
            rename_map[f'calvin_p{p}'] = f'{workload}_calvin_p{p}'

    workload_data.rename(columns=rename_map, inplace=True)

    if final_df is None:
        final_df = workload_data
    else:
        final_df = pd.merge(final_df, workload_data, on='x_var', how='outer')

if final_df is None:
    raise RuntimeError("No data found for the requested scenario and environment.")

final_df = final_df.sort_values('x_var').reset_index(drop=True)

fig = plt.figure(figsize=(8, 3))
gs = fig.add_gridspec(1, 2, width_ratios=[1, 1], wspace=0.3)
axes = [fig.add_subplot(gs[0, 0]), fig.add_subplot(gs[0, 1])]

for workload in VALID_WORKLOADS:
    # copy x_var so we can modify positions for specific workloads (e.g. smallbank)
    x = final_df['x_var'].copy()
    # keep a separate plotting x-vector so original 'x_var' remains available
    new_x_vars = x.copy()

    # mask for rows that have throughput data for either system
    mask_tp = final_df[[f'{workload}_detock', f'{workload}_calvin']].notna().any(axis=1)

    # Adjust x positions per-workload (same mapping as in benchmark_comparison.py)
    if workload == 'dsh':
        new_x_vars.loc[mask_tp] = (1 / 100) * 100 * final_df.loc[mask_tp, 'x_var']
    elif workload == 'pps':
        new_x_vars.loc[mask_tp] = (58 / 100) * final_df.loc[mask_tp, 'x_var']
    elif workload == 'smallbank':
        new_x_vars.loc[mask_tp] = (20 / 100) * final_df.loc[mask_tp, 'x_var']
    elif workload == 'movr':
        new_x_vars.loc[mask_tp] = (71 / 100) * final_df.loc[mask_tp, 'x_var']

    detock_col = f'{workload}_detock'
    calvin_col = f'{workload}_calvin'
    color = WORKLOAD_COLORS[workload]

    # Plot throughput (use adjusted x positions)
    if detock_col in final_df.columns:
        mask_d = final_df[detock_col].notna()
        axes[0].plot(new_x_vars[mask_d], final_df.loc[mask_d, detock_col], label=f'{BENCHMARK_DISPLAY_NAME_MAP[workload]} Detock', color=color, linestyle='--')
        if calvin_col in final_df.columns:
            mask_c = final_df[calvin_col].notna()
            axes[0].plot(
                new_x_vars[mask_c],
                final_df.loc[mask_c, calvin_col],
                linestyle='-',
                color=color,
                label=f'{BENCHMARK_DISPLAY_NAME_MAP[workload]} Calvin'
            )

    # Plot latency only for Movie on the right subplot (use adjusted x positions)
    if workload != 'movie':
        continue

    detock_lsh_p50 = f'{workload}_detock_lsh_p50'
    detock_lsh_p95 = f'{workload}_detock_lsh_p95'
    detock_fsh_p50 = f'{workload}_detock_fsh_p50'
    detock_fsh_p95 = f'{workload}_detock_fsh_p95'
    detock_mh_p50 = f'{workload}_detock_mh_p50'
    detock_mh_p95 = f'{workload}_detock_mh_p95'
    calvin_p50 = f'{workload}_calvin_p50'
    calvin_p95 = f'{workload}_calvin_p95'

    # Detock: LSH and MH — use the same linestyle for p50 and p95; p95 lighter (lower alpha)
    if detock_lsh_p50 in final_df.columns:
        mask_lsh = final_df[detock_lsh_p50].notna()
        axes[1].plot(new_x_vars[mask_lsh], final_df.loc[mask_lsh, detock_lsh_p50], label=f'{BENCHMARK_DISPLAY_NAME_MAP[workload]} Detock LSH', color=LATENCY_ABLATION_COLORS['lsh'], linewidth=2, linestyle='--')
        axes[1].plot(new_x_vars[mask_lsh], final_df.loc[mask_lsh, detock_lsh_p95], color=LATENCY_ABLATION_COLORS['lsh'], linewidth=2, linestyle='--', alpha=0.5)
    
    if detock_mh_p50 in final_df.columns:
        mask_mh = final_df[detock_mh_p50].notna()
        axes[1].plot(new_x_vars[mask_mh], final_df.loc[mask_mh, detock_mh_p50], label=f'{BENCHMARK_DISPLAY_NAME_MAP[workload]} Detock MH', color=LATENCY_ABLATION_COLORS['mh'], linewidth=1.5, linestyle=':')
        axes[1].plot(new_x_vars[mask_mh], final_df.loc[mask_mh, detock_mh_p95], color=LATENCY_ABLATION_COLORS['mh'], linewidth=1.5, linestyle=':', alpha=0.5)

axes[0].set_title('Throughput')
axes[0].set_ylabel('Throughput (txns/s)')
axes[1].set_title('Latency Ablations')
axes[1].set_ylabel('Latency (ms)')

if scenario == 'baseline':
    axes[0].set_xlabel('Geo-distribution (%)')
    axes[1].set_xlabel('Geo-distribution (%)')
elif scenario == 'scalability':
    axes[0].set_xlabel('Input throughput (txns/s)')
    axes[1].set_xlabel('Input throughput (txns/s)')
    axes[0].xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, pos: f'{x/1000:.0f}k'))
    axes[1].xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, pos: f'{x/1000:.0f}k'))
else:
    axes[0].set_xlabel('x_var')
    axes[1].set_xlabel('x_var')

axes[0].yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, pos: f'{x/1000:.0f}k'))
axes[0].set_ylim(bottom=0)
axes[0].grid(which='major', axis='y')
axes[0].legend(loc='upper center', ncol=1, bbox_to_anchor=(0.5, 1.7))

# Use log scale for latency and set bottom to 10^0
axes[1].set_yscale('log')
axes[1].set_ylim(bottom=1)
axes[1].grid(which='major', axis='y')
axes[1].legend(loc='upper center', ncol=1, bbox_to_anchor=(0.5, 1.5))

plt.tight_layout(rect=[0, 0, 1, 0.7])

output_dir = f'plots/output/{env}/{scenario}'
os.makedirs(output_dir, exist_ok=True)
output_base = f'{output_dir}/benchmark_comparison_ablations'
plt.savefig(output_base + '.png', dpi=300, bbox_inches='tight', pad_inches=0.0)
plt.savefig(output_base + '.pdf', bbox_inches='tight', pad_inches=0.0)

#if show_plot:
#    plt.show()

print("Done")