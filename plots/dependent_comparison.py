import os
import tarfile
import json
import hashlib
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import argparse

plt.rcParams.update({
    'font.size': 14,
    'axes.titlesize': 16,
    'axes.labelsize': 14,
    'xtick.labelsize': 14,
    'ytick.labelsize': 14,
    'legend.fontsize': 14
})

VALID_ENVIRONMENTS = ['local', 'st', 'aws']
VALID_SYSTEMS = ['calvin', 'slog', 'Detock', 'janus', 'crdb']
SYSTEM_DISPLAY_NAME_MAP = {
    'calvin': 'Calvin',
    'slog': 'SLOG',
    'Detock': 'Detock',
    'janus': 'Janus',
    'crdb': 'CRDB'
}

X_AXIS_LIMITS = (0, 100)
THROUGHPUT_YLIM = (0, 10_000)
THROUGHPUT_YLIM_CRDB = (0, 1_000)
LATENCY_YLIM = (1, 10_000)
LATENCY_YLIM_CRDB = (1, 20_000)

CACHE_FILENAME_TEMPLATE = 'dependent_{system}.meta.json'

def compute_raw_data_fingerprint(raw_dir):
    h = hashlib.sha256()
    for root, dirs, files in os.walk(raw_dir):
        files.sort()
        for fname in files:
            file_path = os.path.join(root, fname)
            try:
                stat = os.stat(file_path)
            except OSError:
                continue
            rel_path = os.path.relpath(file_path, raw_dir)
            h.update(rel_path.encode('utf-8'))
            h.update(str(stat.st_size).encode('utf-8'))
            h.update(str(stat.st_mtime_ns).encode('utf-8'))
    return h.hexdigest()

def load_cached_fingerprint(cache_path):
    if not os.path.exists(cache_path):
        return None
    try:
        with open(cache_path, 'r') as f:
            return json.load(f).get('fingerprint')
    except Exception:
        return None

def save_cached_fingerprint(cache_path, fingerprint):
    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    with open(cache_path, 'w') as f:
        json.dump({'fingerprint': fingerprint}, f)

def parse_args():
    parser = argparse.ArgumentParser(description="Extract and plot experiment results for the dependent scenario.")
    parser.add_argument("-w",  "--workload", default='benchx', help="Workload to run (default: benchx)")
    parser.add_argument("-sy", "--system", default='all', choices=VALID_SYSTEMS + ['all'], help="System to plot, or 'all' to generate plots for every valid system")
    parser.add_argument("-e",  "--environment", default='st', choices=VALID_ENVIRONMENTS, help="What type of machine the experiment was run on.")
    parser.add_argument("-ne", "--no_extraction", action="store_true", default=False, help="Whether to skip extraction and just load from CSV")
    parser.add_argument("-cy", "--separate_crdb_y", action="store_true", default=True, help="Whether to use a separate y-axis for CRDB to allow it to auto-scale.")
    return parser.parse_args()

def load_dependent_df(system, args):
    print(f"Loading data for {system}...")
    data_dir = f'plots/data/{args.environment}/{args.workload}/dependent'
    csv_path = os.path.join(data_dir, f'dependent_{system}.csv')
    cache_path = os.path.join(data_dir, CACHE_FILENAME_TEMPLATE.format(system=system))
    sys_display = SYSTEM_DISPLAY_NAME_MAP.get(system, system)
    base_dir = f"plots/raw_data/{args.environment}/{args.workload}/dependent/{sys_display}"

    if args.no_extraction:
        if os.path.exists(csv_path):
            return pd.read_csv(csv_path)
        else:
            print(f"Skipping {system}: no extracted data found at {csv_path}")
            return None

    if os.path.exists(csv_path):
        if os.path.exists(base_dir):
            current_fp = compute_raw_data_fingerprint(base_dir)
            cached_fp = load_cached_fingerprint(cache_path)
            if cached_fp == current_fp:
                print(f"Skipping extraction for {system}: raw data unchanged")
                return pd.read_csv(csv_path)
        else:
            print(f"Skipping extraction for {system}: raw data missing, using existing CSV")
            return pd.read_csv(csv_path)

    if not os.path.exists(base_dir):
        return None

    data = []
    for folder in os.listdir(base_dir):
        print(f"Processing folder: {folder}")
        folder_path = os.path.join(base_dir, folder)
        if not os.path.isdir(folder_path):
            continue

        try:
            dep_percent = int(folder)
        except ValueError:
            continue

        throughput = 0.0
        raw_logs_dir = os.path.join(folder_path, "raw_logs")
        if os.path.isdir(raw_logs_dir):
            for f in os.listdir(raw_logs_dir):
                if "benchmark_container_" in f and f.endswith(".log"):
                    with open(os.path.join(raw_logs_dir, f), "r") as log_file:
                        for line in log_file:
                            if "Avg. TPS: " in line:
                                throughput += float(line.split("Avg. TPS: ")[1].strip())

        latencies = []
        dependent_latencies = []
        non_dependent_latencies = []
        
        total_physical = 0
        total_logical = 0
        
        client_dir = os.path.join(folder_path, "client")
        if os.path.isdir(client_dir):
            for c in os.listdir(client_dir):
                c_path = os.path.join(client_dir, c)
                df = None
                if os.path.isdir(c_path):
                    tx_file = os.path.join(c_path, "transactions.csv")
                    if os.path.exists(tx_file):
                        df = pd.read_csv(tx_file, on_bad_lines='skip')
                elif c.endswith(".tar.gz"):
                    try:
                        with tarfile.open(c_path, "r:gz") as tar:
                            for member in tar.getmembers():
                                if member.name.endswith("transactions.csv"):
                                    f = tar.extractfile(member)
                                    df = pd.read_csv(f, on_bad_lines='skip')
                    except Exception as e:
                        print(f"Failed to read tar {c_path}: {e}")
                        
                if df is not None and 'received_at' in df.columns and 'sent_at' in df.columns:
                    df['received_at'] = pd.to_numeric(df['received_at'], errors='coerce')
                    df['sent_at'] = pd.to_numeric(df['sent_at'], errors='coerce')
                    df['duration'] = df['received_at'] - df['sent_at']
                    
                    valid_df = df.dropna(subset=['duration']).copy()
                    
                    if 'code' in valid_df.columns:
                        valid_df['code'] = valid_df['code'].astype(str)
                        valid_df['dep_id'] = valid_df['code'].str.extract(r'(dep_\d+)')
                        
                        dep_mask = valid_df['dep_id'].notna()
                        
                        non_dep_df = valid_df[~dep_mask]
                        non_dependent_latencies.extend(non_dep_df['duration'].tolist())
                        latencies.extend(non_dep_df['duration'].tolist())
                        
                        total_physical += len(valid_df)
                        total_logical += len(non_dep_df)
                        
                        dep_df = valid_df[dep_mask]
                        if not dep_df.empty:
                            grouped = dep_df.groupby('dep_id').agg(
                                count=('received_at', 'count'),
                                min_sent=('sent_at', 'min'),
                                max_received=('received_at', 'max')
                            )
                            if system == 'crdb':
                                completed_deps = grouped[grouped['count'] == 1].copy()
                            else:
                                completed_deps = grouped[grouped['count'] == 2].copy()
                            total_logical += len(completed_deps)
                            
                            completed_deps['duration'] = completed_deps['max_received'] - completed_deps['min_sent']
                            dependent_latencies.extend(completed_deps['duration'].tolist())
                            latencies.extend(completed_deps['duration'].tolist())
                    else:
                        latencies.extend(valid_df['duration'].tolist())
                        total_physical += len(valid_df)
                        total_logical += len(valid_df)
        throughput_split = throughput
        if system == 'crdb':
            throughput_grouped = throughput
        else:
            throughput_grouped = throughput
            if total_physical > 0:
                throughput_grouped = throughput * (total_logical / total_physical)

        p50_latency = np.percentile(latencies, 50) / 1_000_000.0 if latencies else -1
        p95_latency = np.percentile(latencies, 95) / 1_000_000.0 if latencies else -1
        p99_latency = np.percentile(latencies, 99) / 1_000_000.0 if latencies else -1

        p50_dep = np.percentile(dependent_latencies, 50) / 1_000_000.0 if dependent_latencies else -1
        p95_dep = np.percentile(dependent_latencies, 95) / 1_000_000.0 if dependent_latencies else -1
        p99_dep = np.percentile(dependent_latencies, 99) / 1_000_000.0 if dependent_latencies else -1

        p50_non = np.percentile(non_dependent_latencies, 50) / 1_000_000.0 if non_dependent_latencies else -1
        p95_non = np.percentile(non_dependent_latencies, 95) / 1_000_000.0 if non_dependent_latencies else -1
        p99_non = np.percentile(non_dependent_latencies, 99) / 1_000_000.0 if non_dependent_latencies else -1

        data_point = {
            'dependent_percent': dep_percent,
            'throughput_split': throughput_split,
            'throughput_grouped': throughput_grouped,
            'latency_p50': p50_latency,
            'latency_p95': p95_latency,
            'latency_p99': p99_latency,
            'latency_p50_dependent': p50_dep,
            'latency_p95_dependent': p95_dep,
            'latency_p99_dependent': p99_dep,
            'latency_p50_non_dependent': p50_non,
            'latency_p95_non_dependent': p95_non,
            'latency_p99_non_dependent': p99_non
        }
        data.append(data_point)

    if not data:
        return None

    df = pd.DataFrame(data)
    
    for col in df.columns:
        if col.startswith('latency_') or col.startswith('throughput'):
            df[col] = df[col].round().astype('Int64')
            
    df = df.sort_values(by=['dependent_percent'])
    
    if not args.no_extraction:
        os.makedirs(data_dir, exist_ok=True)
        df.to_csv(csv_path, index=False)
        current_fp = compute_raw_data_fingerprint(base_dir)
        save_cached_fingerprint(cache_path, current_fp)
        print(f"Saved extracted CSV and fingerprint cache for {system}")
        
    return df

def plot_dependent_system(df, system, args, base_out_path):
    os.makedirs(f'plots/data/{args.environment}/{args.workload}/dependent', exist_ok=True)
    if not args.no_extraction:
        df.to_csv(f'plots/data/{args.environment}/{args.workload}/dependent/dependent_{system}.csv', index=False)

    print(f"Generating plots for {system}...")
    fig, axes = plt.subplots(2, 2, figsize=(8, 6))
    ax_non_dep = axes[0, 0]
    ax_dep = axes[0, 1]
    ax_tp_split = axes[1, 0]
    ax_tp_grouped = axes[1, 1]

    tp_split_col = 'throughput_split'
    tp_grouped_col = 'throughput_grouped'
    p50_col = 'latency_p50'
    p95_col = 'latency_p95'
    p50_dep_col = 'latency_p50_dependent'
    p95_dep_col = 'latency_p95_dependent'
    p50_non_col = 'latency_p50_non_dependent'
    p95_non_col = 'latency_p95_non_dependent'
        
    sub_df = df[df[tp_grouped_col] > 0]
    if not sub_df.empty:
        color = plt.rcParams['axes.prop_cycle'].by_key()['color'][VALID_SYSTEMS.index(system) % len(plt.rcParams['axes.prop_cycle'].by_key()['color'])]
        line, = ax_tp_split.plot(sub_df['dependent_percent'], sub_df[tp_split_col], linewidth=2, label=f'{system}', color=color)
        ax_tp_grouped.plot(sub_df['dependent_percent'], sub_df[tp_grouped_col], color=color, linewidth=2, label=f'{system}')
        
        valid_lat_dep = sub_df[sub_df[p50_dep_col] > 0]
        if not valid_lat_dep.empty:
            ax_dep.plot(valid_lat_dep['dependent_percent'], valid_lat_dep[p50_dep_col], color=color, linewidth=2, label=f'{system} dep p50')
            ax_dep.plot(valid_lat_dep['dependent_percent'], valid_lat_dep[p95_dep_col], color=color, linestyle='--', linewidth=2, alpha=0.7, label=f'{system} dep p95')
            
        valid_lat_non = sub_df[sub_df[p50_non_col] > 0]
        if not valid_lat_non.empty:
            ax_non_dep.plot(valid_lat_non['dependent_percent'], valid_lat_non[p50_non_col], color=color, linewidth=2, label=f'{system} non-dep p50')
            ax_non_dep.plot(valid_lat_non['dependent_percent'], valid_lat_non[p95_non_col], color=color, linestyle='--', linewidth=2, alpha=0.7, label=f'{system} non-dep p95')

    sys_display = SYSTEM_DISPLAY_NAME_MAP.get(system, system)
    
    for ax, title in [(ax_tp_split, 'Throughput (split)'), (ax_tp_grouped, 'Throughput (grouped)')]:
        ax.set_title(f'{title} ({sys_display})')
        ax.set_xlabel('Dependent Transactions (%)')
        if ax == ax_tp_split:
            if args.separate_crdb_y and system == 'crdb':
                ax.set_ylabel('Throughput\nCRDB')
            else:
                ax.set_ylabel('Throughput (txns/s)')
        ax.grid(True, linestyle='--', alpha=0.7)
        if args.separate_crdb_y and system == 'crdb':
            ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda y, _: f"{int(y)}"))
        else:
            ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda y, _: f"{y/1000:g}k"))
        ax.set_ylim(bottom=0)
        ax.set_xlim(*X_AXIS_LIMITS)

    for ax, title in [(ax_non_dep, 'Non-Dependent Latency'), (ax_dep, 'Dependent Latency')]:
        ax.set_title(f'{title} ({sys_display})')
        ax.set_xlabel('Dependent Transactions (%)')
        if ax == ax_non_dep:
            if args.separate_crdb_y and system == 'crdb':
                ax.set_ylabel('Latency (s)\nCRDB')
            else:
                ax.set_ylabel('Latency (ms)')
        ax.grid(True, linestyle='--', alpha=0.7)
        if args.separate_crdb_y and system == 'crdb':
            ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda y, _: f"{y/1000:g}"))
        ax.set_ylim(bottom=0)
        ax.set_xlim(*X_AXIS_LIMITS)

    tp_max = max(ax_tp_split.get_ylim()[1], ax_tp_grouped.get_ylim()[1])
    if args.separate_crdb_y and system == 'crdb':
        ax_tp_split.set_ylim(0, THROUGHPUT_YLIM_CRDB[1])
        ax_tp_grouped.set_ylim(0, THROUGHPUT_YLIM_CRDB[1])
    else:
        ax_tp_split.set_ylim(0, tp_max)
        ax_tp_grouped.set_ylim(0, tp_max)

    lat_max = max(ax_non_dep.get_ylim()[1], ax_dep.get_ylim()[1])
    if args.separate_crdb_y and system == 'crdb':
        ax_non_dep.set_ylim(0, LATENCY_YLIM_CRDB[1])
        ax_dep.set_ylim(0, LATENCY_YLIM_CRDB[1])
    else:
        ax_non_dep.set_ylim(0, lat_max)
        ax_dep.set_ylim(0, lat_max)

    handles, labels = ax_tp_split.get_legend_handles_labels()
    h_non, l_non = ax_non_dep.get_legend_handles_labels()
    h_dep, l_dep = ax_dep.get_legend_handles_labels()
    all_handles = handles + h_non + h_dep
    all_labels = labels + l_non + l_dep
    if all_handles:
        fig.legend(all_handles, all_labels, loc='upper center', bbox_to_anchor=(0.5, 1.05), ncol=len(handles) + 2)

    plt.tight_layout(rect=[0, 0, 1, 1])
    out_path = f'{base_out_path}'
    plt.savefig(f'{out_path}.pdf', bbox_inches='tight', pad_inches=0.0)
    plt.savefig(f'{out_path}.png', dpi=300, bbox_inches='tight', pad_inches=0.0)
    print(f"Plots saved successfully to {out_path}.pdf and .png")
    plt.close(fig)

def plot_dependent_all_systems(args):
    system_dfs = []
    out_dir = f'plots/output/{args.environment}/{args.workload}/dependent'
    os.makedirs(f'plots/data/{args.environment}/{args.workload}/dependent', exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)

    for system in VALID_SYSTEMS:
        df = load_dependent_df(system, args)
        if df is None:
            print(f"Skipping {system}: no data found")
        else:
            print(f"Loaded data for {system} with {len(df)} rows")
            if not args.no_extraction:
                df.to_csv(f'plots/data/{args.environment}/{args.workload}/dependent/dependent_{system}.csv', index=False)
        system_dfs.append((system, df))

    print("Generating plots for all systems...")
    fig, axes = plt.subplots(2, 2, figsize=(8, 6))
    ax_non_dep = axes[0, 0]
    ax_dep = axes[0, 1]
    ax_tp_split = axes[1, 0]
    ax_tp_grouped = axes[1, 1]
    
    crdb_ax_tp_split = ax_tp_split.twinx() if args.separate_crdb_y else ax_tp_split
    crdb_ax_tp_grouped = ax_tp_grouped.twinx() if args.separate_crdb_y else ax_tp_grouped
    crdb_ax_non_dep = ax_non_dep.twinx() if args.separate_crdb_y else ax_non_dep
    crdb_ax_dep = ax_dep.twinx() if args.separate_crdb_y else ax_dep

    tp_split_col = 'throughput_split'
    tp_grouped_col = 'throughput_grouped'
    p50_col = 'latency_p50'
    p95_col = 'latency_p95'
    p50_dep_col = 'latency_p50_dependent'
    p95_dep_col = 'latency_p95_dependent'
    p50_non_col = 'latency_p50_non_dependent'
    p95_non_col = 'latency_p95_non_dependent'

    system_colors = {}
    for system, df in system_dfs:
        if df is not None:
            sub_df = df[df[tp_grouped_col] > 0]
            if sub_df.empty:
                continue

            sys_display = SYSTEM_DISPLAY_NAME_MAP.get(system, system)
            
            is_crdb = args.separate_crdb_y and system == 'crdb'
            cur_tp_split = crdb_ax_tp_split if is_crdb else ax_tp_split
            cur_tp_grouped = crdb_ax_tp_grouped if is_crdb else ax_tp_grouped
            cur_non_dep = crdb_ax_non_dep if is_crdb else ax_non_dep
            cur_dep = crdb_ax_dep if is_crdb else ax_dep
            
            color = plt.rcParams['axes.prop_cycle'].by_key()['color'][VALID_SYSTEMS.index(system) % len(plt.rcParams['axes.prop_cycle'].by_key()['color'])]
            line, = cur_tp_split.plot(sub_df['dependent_percent'], sub_df[tp_split_col], linewidth=2, label=f'{sys_display}', color=color)
            cur_tp_grouped.plot(sub_df['dependent_percent'], sub_df[tp_grouped_col], color=color, linewidth=2)
            system_colors[sys_display] = color
            
            valid_lat_dep = sub_df[sub_df[p50_dep_col] > 0]
            if not valid_lat_dep.empty:
                cur_dep.plot(valid_lat_dep['dependent_percent'], valid_lat_dep[p50_dep_col], color=color, linewidth=2)
                cur_dep.plot(valid_lat_dep['dependent_percent'], valid_lat_dep[p95_dep_col], color=color, linestyle='--', linewidth=2, alpha=0.7)
                
            valid_lat_non = sub_df[sub_df[p50_non_col] > 0]
            if not valid_lat_non.empty:
                cur_non_dep.plot(valid_lat_non['dependent_percent'], valid_lat_non[p50_non_col], color=color, linewidth=2)
                cur_non_dep.plot(valid_lat_non['dependent_percent'], valid_lat_non[p95_non_col], color=color, linestyle='--', linewidth=2, alpha=0.7)

    for ax, title in [(ax_tp_split, 'Throughput (split)'), (ax_tp_grouped, 'Throughput (grouped)')]:
        ax.set_title(title)
        ax.set_xlabel('Dependent Transactions (%)')
        if ax == ax_tp_split:
            ax.set_ylabel('Throughput (txns/s)')
        ax.grid(True, linestyle='--', alpha=0.7)
        ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda y, _: f"{y/1000:g}k"))
        ax.set_xlim(*X_AXIS_LIMITS)
        
    for ax, title in [(ax_non_dep, 'Non-Dependent Latency'), (ax_dep, 'Dependent Latency')]:
        ax.set_title(title)
        ax.set_xlabel('Dependent Transactions (%)')
        if ax == ax_non_dep:
            ax.set_ylabel('Latency (ms)')
        ax.grid(True, linestyle='--', alpha=0.7)
        ax.set_xlim(*X_AXIS_LIMITS)
        
    tp_max = max(ax_tp_split.get_ylim()[1], ax_tp_grouped.get_ylim()[1])
    ax_tp_split.set_ylim(0, tp_max)
    ax_tp_grouped.set_ylim(0, tp_max)
    
    lat_max = max(ax_non_dep.get_ylim()[1], ax_dep.get_ylim()[1])
    ax_non_dep.set_ylim(0, lat_max)
    ax_dep.set_ylim(0, lat_max)
    
    if args.separate_crdb_y:
        for ax in [crdb_ax_tp_split, crdb_ax_tp_grouped]:
            ax.set_ylim(0, THROUGHPUT_YLIM_CRDB[1])
            ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda y, _: f"{int(y)}"))
        for ax in [crdb_ax_non_dep, crdb_ax_dep]:
            ax.set_ylim(0, LATENCY_YLIM_CRDB[1])
            ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda y, _: f"{y/1000:g}"))
            
        crdb_ax_tp_grouped.set_ylabel('Throughput\nCRDB', rotation=270, labelpad=40)
        crdb_ax_dep.set_ylabel('Latency (s)\nCRDB', rotation=270, labelpad=40)

    # Create legend with only system names
    handles_tp, labels_tp = ax_tp_split.get_legend_handles_labels()
    if args.separate_crdb_y:
        h_crdb, l_crdb = crdb_ax_tp_split.get_legend_handles_labels()
        for h, l in zip(h_crdb, l_crdb):
            if l not in labels_tp:
                handles_tp.append(h)
                labels_tp.append(l)
    
    if handles_tp:
        fig.legend(handles_tp, labels_tp, loc='upper center', bbox_to_anchor=(0.5, 1.05), ncol=len(labels_tp))

    plt.tight_layout(rect=[0, 0, 1, 1])
    out_path = f'{out_dir}/dependent_all'
    plt.savefig(f'{out_path}.pdf', bbox_inches='tight', pad_inches=0.0)
    plt.savefig(f'{out_path}.png', dpi=300, bbox_inches='tight', pad_inches=0.0)
    print(f"Plots saved successfully to {out_path}.pdf and .png")
    plt.close(fig)
    print("Done")

def extract_and_plot(args):
    if args.system == 'all':
        plot_dependent_all_systems(args)
        return

    df = load_dependent_df(args.system, args)
    if df is None:
        print(f"No valid data found to plot for {args.system}.")
        return

    out_dir = f'plots/output/{args.environment}/{args.workload}/dependent'
    os.makedirs(out_dir, exist_ok=True)
    out_path = f'{out_dir}/dependent_{args.system.lower()}'
    plot_dependent_system(df, args.system, args, out_path)

if __name__ == "__main__":
    args = parse_args()
    extract_and_plot(args)
