import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from matplotlib.patches import Patch
import math
import argparse

BENCHMARK_PROFILING_FILE = 'plots/data/st/survey_motivation.csv'
BENCHX_COMPARISON_FILE = 'plots/data/st/benchx_comparison.csv'
BENCHX_NAME = 'Penteract'  # This is the name that should appear in the plot legend

parser = argparse.ArgumentParser(add_help=True, formatter_class=argparse.ArgumentDefaultsHelpFormatter)

parser.add_argument('-ic', '--include_crdb', default=False, action='store_true', help='Whether to include CockroachDB')
parser.add_argument('-bc', '--benchx_comparison', default=True, action='store_true', help='Whether to include the BenchX comparison bar plot')
parser.add_argument('-e',  '--environment', default='st', choices=['st', 'aws'], help='Whether the data was collected on the ST or AWS cluster. This only affects the output path.')
parser.add_argument('-nr', '--no_rank', default=True, action='store_true', help='Whether to omit Rank Alignment columns in the latex table')

args = parser.parse_args()
include_crdb = args.include_crdb
benchx_comparison = args.benchx_comparison
environment = args.environment
include_rank = not args.no_rank

BENCHMARK_PROFILING_FILE = f'plots/data/{environment}/survey_motivation.csv'
BENCHX_COMPARISON_FILE = f'plots/data/{environment}/benchx_comparison.csv'

if include_crdb:
    db_map = {
        'Calvin': 'Calvin',
        'SLOG': 'SLOG',
        'Detock': 'Detock',
        'Janus': 'Janus',
        'CRDB': 'CockroachDB'  # This matches the CSV 'System' column to the desired label
    }
    databases = ['Calvin', 'SLOG', 'Detock', 'Janus', 'CockroachDB']
    display_names = ['Calvin', 'SLOG', 'Detock', 'Janus', 'CockroachDB']
    colors = ['tab:blue', 'tab:orange', 'tab:green', 'tab:red', 'tab:brown']
    width = 0.15  # Width of each bar
    legend_rows = 2
else:
    db_map = {
        'Calvin': 'Calvin',
        'SLOG': 'SLOG',
        'Detock': 'Detock',
        'Janus': 'Janus',
    }
    databases = ['Calvin', 'SLOG', 'Detock', 'Janus']
    display_names = ['Calvin', 'SLOG', 'Detock', 'Janus']
    colors = ['tab:blue', 'tab:orange', 'tab:green', 'tab:red']
    width = 0.2  # Width of each bar
    legend_rows = 1

data = pd.read_csv(BENCHMARK_PROFILING_FILE)

def update_benchx_comparison_file():
    counts_file = 'tools/mimic_client_counts.csv'
    mimic_dir = f'plots/data/{environment}/benchx/mimic'
    
    if not os.path.exists(counts_file) or not os.path.exists(BENCHX_COMPARISON_FILE):
        return
        
    counts_df = pd.read_csv(counts_file)
    benchx_df = pd.read_csv(BENCHX_COMPARISON_FILE)
    
    BM_MAP = {
        'TPC-C': 'tpcc',
        'YCSB': 'ycsb',
        'PPS': 'pps',
        'SmallBank': 'smallbank',
        'MovR': 'movr',
        'DS Hotels': 'dsh',
        'DS Movie': 'movie'
    }
    
    systems_in_counts = [c for c in counts_df.columns if c != 'TargetBenchmark']
    
    SYSTEM_MAP = {
        'Calvin': 'calvin',
        'SLOG': 'slog',
        'Detock': 'Detock',
        'Janus': 'janus'
    }
    
    for _, row in counts_df.iterrows():
        orig_bm = row['TargetBenchmark']
        if orig_bm not in BM_MAP:
            continue
            
        mimic_csv = os.path.join(mimic_dir, f"mimic_{BM_MAP[orig_bm]}.csv")
        if not os.path.exists(mimic_csv):
            continue
            
        mimic_df = pd.read_csv(mimic_csv)
        
        for sys in systems_in_counts:
            count_val = row[sys]
            if pd.isna(count_val):
                continue
                
            x_val = float(count_val)
            
            match = mimic_df[mimic_df['x_val'] == x_val]
            if match.empty:
                continue
            
            idx = benchx_df.index[benchx_df['System'] == sys]
            if not idx.empty:
                idx = idx[0]
                mimic_sys = SYSTEM_MAP.get(sys, sys.lower())
                tp_col = f"{mimic_sys}_throughput"
                p50_col = f"{mimic_sys}_p50"
                p99_col = f"{mimic_sys}_p99"
                
                if tp_col in match.columns and not pd.isna(match.iloc[0][tp_col]) and match.iloc[0][tp_col] != -1:
                    benchx_df.at[idx, orig_bm] = match.iloc[0][tp_col]
                if p50_col in match.columns and not pd.isna(match.iloc[0][p50_col]) and match.iloc[0][p50_col] != -1:
                    benchx_df.at[idx, f"{orig_bm}_p50"] = match.iloc[0][p50_col]
                if p99_col in match.columns and not pd.isna(match.iloc[0][p99_col]) and match.iloc[0][p99_col] != -1:
                    benchx_df.at[idx, f"{orig_bm}_p99"] = match.iloc[0][p99_col]
                    
    benchx_df.to_csv(BENCHX_COMPARISON_FILE, index=False)

original_width = width
for benchx_comparison in [False, True]:
    width = original_width
    if benchx_comparison:
        figsize = (10, 3.7)
        output_path = f'plots/output/{environment}/benchx_comparison'
        update_benchx_comparison_file()
        benchx_data = pd.read_csv(BENCHX_COMPARISON_FILE)
        for w in ['BenchX1', 'BenchX2']:
            if w not in benchx_data.columns:
                benchx_data[w] = np.nan
                benchx_data[f"{w}_p50"] = np.nan
                benchx_data[f"{w}_p99"] = np.nan
        width /= 2  # Halve the width for the comparison plot
    else:
        figsize = (5, 3.7)
        output_path = f'plots/output/{environment}/survey_motivation'

    # ALL
    #workloads = ['TPC-C', 'YCSB', 'PPS', 'SmallBank', 'MovR', 'DS Movie', 'DS Hotels', 'BenchX1', 'BenchX2']
    if not benchx_comparison:
        workloads = ['TPC-C', 'YCSB', 'SmallBank', 'DS Movie', 'BenchX1', 'BenchX2']
    else:
        workloads = ['TPC-C', 'YCSB', 'PPS', 'SmallBank', 'MovR', 'DS Movie', 'DS Hotels']
    workload_display_names = {
        'TPC-C': 'TPC-C',
        'YCSB': 'YCSB',
        'PPS': 'PPS',
        'SmallBank': 'SmallBank',
        'MovR': 'MovR',
        'DS Movie': 'DS\nMovie',
        'DS Hotels': 'DS\nHotels',
        'BenchX1': f'{BENCHX_NAME}\nConfig 1',
        'BenchX2': f'{BENCHX_NAME}\nConfig 2'
    }
    workload_labels = [workload_display_names[w] for w in workloads]
    if benchx_comparison:
        workload_labels = [workload_labels.replace('\n', ' ') for workload_labels in workload_labels]
    x = np.arange(len(workloads))  # Label locations

    # Configure Matplotlib global font size
    plt.rcParams.update({
        'font.size': 9,        # Increase font size for better readability
        'axes.titlesize': 10,
        'axes.labelsize': 9,
        'xtick.labelsize': 8,
        'ytick.labelsize': 8,
        'legend.fontsize': 9
    })

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=figsize, sharex=True)

    # Plot bars for each database
    for i, (csv_name, display_name) in enumerate(db_map.items()):
        # Get the row for this system
        row = data[data['System'] == csv_name]
        group_width = width * (2 if benchx_comparison else 1)
        base_offset = i * group_width - (len(db_map) * group_width) / 2 + group_width / 2
        if not row.empty:
            # 1. Throughput Data
            # Extract values and fill NaNs with 0
            tp_vals = np.nan_to_num(row[workloads].values.flatten().astype(float), nan=0.0)

            # 2. Latency Data (p50 for bars, p99 for whiskers)
            p50_cols = [f"{w}_p50" for w in workloads]
            p99_cols = [f"{w}_p99" for w in workloads]

            p50_vals = np.nan_to_num(row[p50_cols].values.flatten().astype(float), nan=0.0)
            p99_vals = np.nan_to_num(row[p99_cols].values.flatten().astype(float), nan=0.0)

            # Calculate the length *above* the bar
            above_length = np.maximum(0, p99_vals - p50_vals)
            # We need an array of zeros for the length *below* the bar
            below_length = np.zeros(len(workloads))
            # Combine them into the format [(below), (above)]
            yerr_onesided = np.stack([below_length, above_length])
        else:
            # If the system isn't found at all, plot zeros
            tp_vals = np.zeros(len(workloads))
            p50_vals = np.zeros(len(workloads))
            yerr_onesided = np.zeros((2, len(workloads)))

        ### Original benchmark bars
        if benchx_comparison:
            main_offset = base_offset - width / 2
        else:
            main_offset = base_offset
        # Plot Throughput (Top)
        ax1.bar(x + main_offset, tp_vals, width, label=display_name, color=colors[i], edgecolor='black', linewidth=0.5)
        # Plot Latency (Bottom)
        ax2.bar(x + main_offset, p50_vals, width, color=colors[i], edgecolor='black', linewidth=0.5,
                yerr=yerr_onesided, error_kw={'lw': 0.8, 'capsize': 2, 'capthick': 0.8, 'ecolor': 'black'})

        ### BenchX comparison bars (if enabled)
        if benchx_comparison:
            benchx_row = benchx_data[benchx_data['System'] == csv_name]

            if not benchx_row.empty:
                benchx_tp_vals = np.nan_to_num(benchx_row[workloads].values.flatten().astype(float), nan=0.0)

                benchx_p50_vals = np.nan_to_num(benchx_row[p50_cols].values.flatten().astype(float), nan=0.0)
                benchx_p99_vals = np.nan_to_num(benchx_row[p99_cols].values.flatten().astype(float), nan=0.0)

                benchx_above = np.maximum(0, benchx_p99_vals - benchx_p50_vals)
                benchx_yerr = np.stack([np.zeros(len(workloads)), benchx_above])
            else:
                benchx_tp_vals = np.zeros(len(workloads))
                benchx_p50_vals = np.zeros(len(workloads))
                benchx_yerr = np.zeros((2, len(workloads)))

            comparison_offset = base_offset + width / 2
            # Use lighter shades for the comparison bars
            ax1.bar(x + comparison_offset, benchx_tp_vals, width, color=colors[i], alpha=0.4, edgecolor='black', linewidth=0.5)

            ax2.bar(x + comparison_offset, benchx_p50_vals, width, color=colors[i], alpha=0.4, edgecolor='black', linewidth=0.5,
                yerr=benchx_yerr, error_kw={'lw': 0.8, 'capsize': 2, 'capthick': 0.8, 'ecolor': 'black'})

    # --- Styling Ax1 (Throughput) ---
    ax1.set_ylabel('Throughput (txns/s)')
    ax1.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, p: f'{x/1000:.0f}k' if x >= 1000 else f'{x:.0f}'))
    ax1.set_ylim(0, 60_000)
    ax1.set_xticks(x)
    ax1.set_xticklabels(workload_labels)
    ax1.grid(axis='y', alpha=0.7)
    ax1.set_axisbelow(True) # Ensure grid is behind bars

    if benchx_comparison:
        legend_handles = []
        for i, db in enumerate(display_names):
            legend_handles.append(Patch(facecolor=colors[i], edgecolor='black', label=f'{db} (Org benchmark)'))
            legend_handles.append(Patch(facecolor=colors[i], edgecolor='black', alpha=0.4, label=f'{db} ({BENCHX_NAME})'))
        ax1.legend(handles=legend_handles, loc='upper center', bbox_to_anchor=(0.5, 1.4), ncol=len(display_names))
    else:
        ax1.legend(loc='upper center', bbox_to_anchor=(0.5, 1.3), ncol=math.ceil(len(databases)/legend_rows))

    # --- Styling Ax2 (Latency) ---
    ax2.set_ylabel('Latency (ms)\n(p50 + p99 whisker)')
    ax2.set_xlabel('Benchmark')
    ax2.set_xticks(x)
    ax2.set_xticklabels(workload_labels)
    ax2.grid(axis='y', alpha=0.7)
    # Log scale might be useful if the whiskers explode
    ax2.set_yscale('log')
    ax2.set_ylim(bottom=1)
    ax2.set_axisbelow(True) # Ensure grid is behind bars
    
    if not benchx_comparison:
        # Draw a vertical line between DS Movie (index 3) and BenchX1 (index 4)
        ax1.axvline(x=3.5, color='black', linestyle='--', linewidth=1, alpha=0.5)
        ax2.axvline(x=3.5, color='black', linestyle='--', linewidth=1, alpha=0.5)

    plt.tight_layout()

    jpg_path = output_path + '.jpg'
    pdf_path = output_path + '.pdf'
    plt.savefig(jpg_path, dpi=300, bbox_inches='tight', pad_inches=0)
    plt.savefig(pdf_path, bbox_inches='tight', pad_inches=0)

    print(f"Done {benchx_comparison}")
    plt.close(fig)

    if benchx_comparison:
        print("\nCalculating Fidelity Metrics...")
        results = []
        # Exclude BenchX1/BenchX2 from workloads for metrics, use original 7 only
        metric_workloads = ['TPC-C', 'YCSB', 'PPS', 'SmallBank', 'MovR', 'DS Movie', 'DS Hotels']
        for wl in metric_workloads:
            orig_tps, mimic_tps = [], []
            orig_p50s, mimic_p50s = [], []
            orig_p99s, mimic_p99s = [], []
            
            for sys in db_map.keys():
                row_o = data[data['System'] == sys]
                row_m = benchx_data[benchx_data['System'] == sys]
                
                if not row_o.empty and not row_m.empty:
                    if wl in row_o.columns and wl in row_m.columns:
                        val_o_tp = row_o[wl].values[0]
                        val_m_tp = row_m[wl].values[0]
                        val_o_p50 = row_o[f'{wl}_p50'].values[0]
                        val_m_p50 = row_m[f'{wl}_p50'].values[0]
                        val_o_p99 = row_o[f'{wl}_p99'].values[0]
                        val_m_p99 = row_m[f'{wl}_p99'].values[0]
                        
                        if not pd.isna(val_o_tp) and not pd.isna(val_m_tp) and val_o_tp > 0 and val_m_tp > 0:
                            orig_tps.append(float(val_o_tp))
                            mimic_tps.append(float(val_m_tp))
                            orig_p50s.append(float(val_o_p50))
                            mimic_p50s.append(float(val_m_p50))
                            orig_p99s.append(float(val_o_p99))
                            mimic_p99s.append(float(val_m_p99))
            
            if len(orig_tps) > 1:
                rho_tp = pd.Series(orig_tps).rank().corr(pd.Series(mimic_tps).rank(), method='pearson')
                rho_p50 = pd.Series(orig_p50s).rank().corr(pd.Series(mimic_p50s).rank(), method='pearson')
            else:
                rho_tp, rho_p50 = np.nan, np.nan
                
            if len(orig_tps) > 0:
                orig_tps_arr, mimic_tps_arr = np.array(orig_tps), np.array(mimic_tps)
                orig_p50_arr, mimic_p50_arr = np.array(orig_p50s), np.array(mimic_p50s)
                orig_p99_arr, mimic_p99_arr = np.array(orig_p99s), np.array(mimic_p99s)
                
                re_tp = np.mean(np.abs(mimic_tps_arr - orig_tps_arr) / orig_tps_arr)
                mae_log_p50 = np.mean(np.abs(np.log10(mimic_p50_arr) - np.log10(orig_p50_arr)))
                mae_log_p99 = np.mean(np.abs(np.log10(mimic_p99_arr) - np.log10(orig_p99_arr)))
            else:
                re_tp, mae_log_p50, mae_log_p99 = np.nan, np.nan, np.nan
                
            results.append({
                'Benchmark': wl, 'rho_tp': rho_tp, 'rho_p50': rho_p50,
                're_tp': re_tp, 'mae_log_p50': mae_log_p50, 'mae_log_p99': mae_log_p99
            })

        res_df = pd.DataFrame(results)
        
        if include_rank:
            latex = "\\begin{tabular}{l *{5}{c}}\n"
            latex += "\\toprule\n"
            latex += "& \\multicolumn{2}{c}{\\textbf{Rank Alignment ($\\rho$)}} & \\multicolumn{3}{c}{\\textbf{Magnitude Error}} \\\\\n"
            latex += "\\cmidrule(lr){2-3} \\cmidrule(lr){4-6}\n"
            latex += "\\textbf{Benchmark} & \\textbf{Throughput} & \\textbf{Latency (p50)} & \\textbf{Throughput ($\\text{RE}_{\\text{T}}$)} & \\textbf{p50 ($\\text{MAE}_{\\log}$)} & \\textbf{p99 ($\\text{MAE}_{\\log}$)} \\\\\n"
            latex += "\\midrule\n"
        else:
            latex = "\\begin{tabular}{l *{3}{c}}\n"
            latex += "\\toprule\n"
            latex += "& \\multicolumn{3}{c}{\\textbf{Magnitude Error}} \\\\\n"
            latex += "\\cmidrule(lr){2-4}\n"
            latex += "\\textbf{Benchmark} & \\textbf{Throughput ($\\text{RE}_{\\text{T}}$)} & \\textbf{p50 ($\\text{MAE}_{\\log}$)} & \\textbf{p99 ($\\text{MAE}_{\\log}$)} \\\\\n"
            latex += "\\midrule\n"
            
        for idx, row in res_df.iterrows():
            if include_rank:
                latex += f"\\textbf{{{row['Benchmark']}}} & {row['rho_tp']:.2f} & {row['rho_p50']:.2f} & {row['re_tp']:.2f} & {row['mae_log_p50']:.2f} & {row['mae_log_p99']:.2f} \\\\\n"
            else:
                latex += f"\\textbf{{{row['Benchmark']}}} & {row['re_tp']:.2f} & {row['mae_log_p50']:.2f} & {row['mae_log_p99']:.2f} \\\\\n"
            
        avg = res_df.mean(numeric_only=True)
        latex += "\\midrule\n"
        if include_rank:
            latex += f"\\textbf{{Average}} & \\textbf{{{avg['rho_tp']:.2f}}} & \\textbf{{{avg['rho_p50']:.2f}}} & \\textbf{{{avg['re_tp']:.2f}}} & \\textbf{{{avg['mae_log_p50']:.2f}}} & \\textbf{{{avg['mae_log_p99']:.2f}}} \\\\\n"
        else:
            latex += f"\\textbf{{Average}} & \\textbf{{{avg['re_tp']:.2f}}} & \\textbf{{{avg['mae_log_p50']:.2f}}} & \\textbf{{{avg['mae_log_p99']:.2f}}} \\\\\n"
        
        latex += "\\bottomrule\n\\end{tabular}%\n"
        
        print("\n--- LATEX TABLE ---")
        print(latex)
        print("-------------------\n")