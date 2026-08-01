import os
import tarfile
import json
import hashlib
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import matplotlib.lines as mlines
import argparse

# Apply rcParams FIRST, before creating any plots
plt.rcParams.update({
    'font.size': 16,
    'axes.titlesize': 18,
    'axes.labelsize': 16,
    'xtick.labelsize': 16,
    'ytick.labelsize': 16,
    'legend.fontsize': 16
})

VALID_ENVIRONMENTS = ['local', 'st', 'aws']
VALID_SYSTEMS = ['calvin', 'slog', 'Detock', 'janus', 'crdb']
VALID_INTENSITIES = [1, 2, 5, 20]
SYSTEM_DISPLAY_NAME_MAP = {
    'Detock': 'Detock',
    'slog': 'SLOG',
    'calvin': 'Calvin',
    'janus': 'Janus',
    'crdb': 'CRDB'
}

TXN_FAMILY_MAP = {
    'Short Read & Write': ['new_order'],
    'Short Read-Only': ['order_status', 'get_customer_by_name', 'get_item_by_name'],
    'Large Read-Only': ['stock_level'],
    'Insert-Only': ['insert_only'],
    'Delete-Only': ['delete_only']
}

X_AXIS_LIMITS = (0, 100)
THROUGHPUT_YLIM = (0, 10_000)
THROUGHPUT_YLIM_CRDB = (0, None)
LATENCY_YLIM = (1, 10_000)
LATENCY_YLIM_CRDB = (1, None)
LATENCY_YLIM_FAMILY = (1, 1_000)

CACHE_FILENAME_TEMPLATE = 'scope_{system}.meta.json'

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
    parser = argparse.ArgumentParser(description="Extract and plot experiment results for the scope scenario.")
    parser.add_argument("-w",  "--workload", default='benchx', help="Workload to run (default: benchx)")
    parser.add_argument("-sy", "--system", default='all', choices=VALID_SYSTEMS + ['all'], help="System to plot, or 'all' to generate plots for every valid system")
    parser.add_argument("-e",  "--environment", default='st', choices=VALID_ENVIRONMENTS, help="What type of machine the experiment was run on.")
    parser.add_argument("-ne", "--no_extraction", action="store_true", default=False, help="Whether to skip extraction and just load from CSV")
    parser.add_argument("-md", "--merge_dependent", action="store_true", default=True, help="Whether to merge dependent transactions in the plots.")
    parser.add_argument("-cy", "--separate_crdb_y", action="store_true", default=True, help="Whether to use a separate y-axis for CRDB to allow it to auto-scale.")
    return parser.parse_args()

def load_scope_df(system, args):
    print(f"Loading data for {system}...")
    data_dir = f'plots/data/{args.environment}/{args.workload}/scope'
    csv_path = os.path.join(data_dir, f'scope_{system}.csv')
    cache_path = os.path.join(data_dir, CACHE_FILENAME_TEMPLATE.format(system=system))
    sys_display = SYSTEM_DISPLAY_NAME_MAP.get(system, system)
    base_dir = f"plots/raw_data/{args.environment}/{args.workload}/scope/{sys_display}"

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
    for folder in sorted(os.listdir(base_dir)):
        print(f"Processing folder: {folder}")
        folder_path = os.path.join(base_dir, folder)
        if not os.path.isdir(folder_path):
            continue

        parts = folder.split('_')
        if len(parts) != 5:
            continue

        try:
            intensity = int(parts[0])
            base_x = float(parts[1])
        except ValueError:
            continue

        if intensity not in VALID_INTENSITIES:
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

        client_dir = os.path.join(folder_path, "client")
        if throughput == 0.0 and system == 'crdb':
            total_committed = 0
            if os.path.isdir(client_dir):
                for c in os.listdir(client_dir):
                    c_path = os.path.join(client_dir, c)
                    summary_path = os.path.join(c_path, "summary.csv")
                    if os.path.exists(summary_path):
                        try:
                            s_df = pd.read_csv(summary_path)
                            if 'committed' in s_df.columns:
                                total_committed += s_df['committed'].sum()
                        except Exception:
                            pass
            
            duration = 60
            if os.path.isdir(raw_logs_dir):
                cmd_log = os.path.join(raw_logs_dir, "benchmark_cmd.log")
                if os.path.exists(cmd_log):
                    try:
                        with open(cmd_log, "r") as log_file:
                            for line in log_file:
                                if " --duration " in line:
                                    duration = int(line.split(" --duration ")[1].split()[0])
                                    break
                    except Exception:
                        pass
            if duration > 0:
                throughput = total_committed / duration

        family_latencies = {fam: [] for fam in TXN_FAMILY_MAP}
        family_counts = {fam: 0 for fam in TXN_FAMILY_MAP}
        total_txns = 0
        
        folder_physical_txns = 0
        folder_logical_txns = 0

        # Optimization: Map codes for fast extraction
        all_codes = []
        code_to_fam = {}
        for fam, codes in TXN_FAMILY_MAP.items():
            for c in codes:
                all_codes.append(c)
                code_to_fam[c] = fam
        code_pattern = f"({'|'.join(all_codes)})"

        latencies = []
        client_dir = os.path.join(folder_path, "client")
        if os.path.isdir(client_dir):
            for c in os.listdir(client_dir):
                c_path = os.path.join(client_dir, c)
                df = None
                if os.path.isdir(c_path):
                    tx_file = os.path.join(c_path, "transactions.csv")
                    if os.path.exists(tx_file):
                        # Optimize pd.read_csv to only load needed cols if possible, 
                        # but some files might not have all cols, so we use a lambda or just load normally and skip bad lines
                        try:
                            df = pd.read_csv(tx_file, usecols=lambda col: col in ['received_at', 'sent_at', 'code'], on_bad_lines='skip')
                        except Exception:
                            df = pd.read_csv(tx_file, on_bad_lines='skip')
                elif c.endswith(".tar.gz"):
                    try:
                        with tarfile.open(c_path, "r:gz") as tar:
                            for member in tar.getmembers():
                                if member.name.endswith("transactions.csv"):
                                    f = tar.extractfile(member)
                                    try:
                                        df = pd.read_csv(f, usecols=lambda col: col in ['received_at', 'sent_at', 'code'], on_bad_lines='skip')
                                    except Exception:
                                        f.seek(0)
                                        df = pd.read_csv(f, on_bad_lines='skip')
                    except Exception as e:
                        print(f"Failed to read tar {c_path}: {e}")
                        
                if df is not None and 'received_at' in df.columns and 'sent_at' in df.columns:
                    folder_physical_txns += len(df)
                    df['received_at'] = pd.to_numeric(df['received_at'], errors='coerce')
                    df['sent_at'] = pd.to_numeric(df['sent_at'], errors='coerce')
                    
                    if args.merge_dependent and 'code' in df.columns:
                        df['dep_id'] = df['code'].astype(str).str.extract(r'dep_(\d+)')
                        independent = df[df['dep_id'].isna()].copy()
                        dependent = df[df['dep_id'].notna()].copy()
                        
                        if not dependent.empty:
                            dependent = dependent.sort_values('sent_at')
                            merged = dependent.groupby('dep_id').agg(
                                received_at=('received_at', 'max'),
                                sent_at=('sent_at', 'min'),
                                code=('code', 'last')
                            ).reset_index()
                            
                            merged = merged.drop(columns=['dep_id'])
                            independent = independent.drop(columns=['dep_id'])
                            df = pd.concat([independent, merged], ignore_index=True)
                        else:
                            df = df.drop(columns=['dep_id'])
                            
                    folder_logical_txns += len(df)
                    df['duration'] = df['received_at'] - df['sent_at']
                    
                    valid_df = df.dropna(subset=['duration']).copy()
                    latencies.extend(valid_df['duration'].tolist())
                    total_txns += len(valid_df)
                    
                    if 'code' in valid_df.columns:
                        valid_df['code'] = valid_df['code'].astype(str)
                        extracted = valid_df['code'].str.extract(code_pattern, expand=False)
                        valid_df['family'] = extracted.map(code_to_fam)
                        
                        for fam, fam_df in valid_df.dropna(subset=['family']).groupby('family'):
                            family_latencies[fam].extend(fam_df['duration'].tolist())
                            family_counts[fam] += len(fam_df)

        if folder_physical_txns > 0:
            throughput = throughput * (folder_logical_txns / folder_physical_txns)

        p50_latency = -1
        p95_latency = -1
        p99_latency = -1
        if latencies:
            p50_latency = np.percentile(latencies, 50) / 1_000_000.0
            p95_latency = np.percentile(latencies, 95) / 1_000_000.0
            p99_latency = np.percentile(latencies, 99) / 1_000_000.0

        data_point = {
            'intensity': intensity,
            'base_x': base_x,
            'throughput': throughput,
            'latency_p50': p50_latency,
            'latency_p95': p95_latency,
            'latency_p99': p99_latency
        }
        
        for fam in TXN_FAMILY_MAP.keys():
            lats = family_latencies[fam]
            data_point[f'latency_p50_{fam.replace(" ", "_")}'] = np.percentile(lats, 50) / 1_000_000.0 if lats else -1
            data_point[f'latency_p95_{fam.replace(" ", "_")}'] = np.percentile(lats, 95) / 1_000_000.0 if lats else -1
            data_point[f'latency_p99_{fam.replace(" ", "_")}'] = np.percentile(lats, 99) / 1_000_000.0 if lats else -1
            data_point[f'throughput_{fam.replace(" ", "_")}'] = throughput * (family_counts[fam] / total_txns) if total_txns > 0 else -1

        data.append(data_point)

    if not data:
        return None

    df = pd.DataFrame(data)
    df['geo_dist'] = ((1.0 - df['base_x']) * 100).round().astype(int)
    
    for col in df.columns:
        if col.startswith('latency_') or col.startswith('throughput'):
            df[col] = df[col].round().astype('Int64')
            
    df = df.sort_values(by=['intensity', 'geo_dist'])

    if not args.no_extraction:
        os.makedirs(data_dir, exist_ok=True)
        df.to_csv(csv_path, index=False)
        current_fp = compute_raw_data_fingerprint(base_dir)
        save_cached_fingerprint(cache_path, current_fp)
        print(f"Saved extracted CSV and fingerprint cache for {system}")

    return df

def plot_scope_system(df, system, args, base_out_path):
    os.makedirs(f'plots/data/{args.environment}/{args.workload}/scope', exist_ok=True)
    if not args.no_extraction:
        df.to_csv(f'plots/data/{args.environment}/{args.workload}/scope/scope_{system}.csv', index=False)

    families_to_plot = [None] + list(TXN_FAMILY_MAP.keys())

    for family in families_to_plot:
        print(f"Generating plots for {system}" + (f" ({family})" if family else "") + "...")
        figsize_y = 8 if family is None else 8.25
        fig, axes = plt.subplots(2, 1, figsize=(15, figsize_y), sharex=True)

        intensities = sorted(df['intensity'].unique())

        for intensity in intensities:
            sub_df = df[df['intensity'] == intensity]
            
            if family is None:
                tp_col = 'throughput'
                p50_col = 'latency_p50'
                p95_col = 'latency_p95'
            else:
                fam_clean = family.replace(" ", "_")
                tp_col = f'throughput_{fam_clean}'
                p50_col = f'latency_p50_{fam_clean}'
                p95_col = f'latency_p95_{fam_clean}'
            
            sub_df = sub_df[sub_df[tp_col] > 0]
            if sub_df.empty:
                continue

            line, = axes[0].plot(sub_df['geo_dist'], sub_df[tp_col], linewidth=2, label=f'{intensity}')
            color = line.get_color()
            
            valid_lat = sub_df[sub_df[p50_col] > 0]
            if not valid_lat.empty:
                axes[1].plot(valid_lat['geo_dist'], valid_lat[p50_col], color=color, linewidth=2, label=f'{intensity}')
                axes[1].plot(valid_lat['geo_dist'], valid_lat[p95_col], color=color, linestyle='--', linewidth=2, alpha=0.7)

        sys_display = SYSTEM_DISPLAY_NAME_MAP.get(system, system)
        
        axes[0].set_title(f'Throughput ({sys_display})')
        axes[0].set_xlabel('Geo-distribution (%)')
        axes[0].set_ylabel('Throughput (txns/s)')
        axes[0].grid(True, linestyle='--', alpha=0.7)
        axes[0].yaxis.set_major_formatter(ticker.FuncFormatter(lambda y, _: f"{y/1000:g}k"))
        max_y = THROUGHPUT_YLIM[1] if family is None else THROUGHPUT_YLIM[1] / 4
        axes[0].set_ylim(bottom=0, top=max_y)

        axes[1].set_title(f'Latency ({sys_display})')
        axes[1].set_xlabel('Geo-distribution (%)')
        axes[1].set_ylabel('Latency (ms)')
        axes[1].grid(True, linestyle='--', alpha=0.7)
        axes[1].set_yscale('log')
        axes[1].set_ylim(bottom=LATENCY_YLIM[0], top=LATENCY_YLIM[1])

        handles, labels = axes[0].get_legend_handles_labels()
        if handles:
            handles.insert(0, mlines.Line2D([], [], color='none'))
            labels.insert(0, 'Keys:')
            if family is not None:
                fig.suptitle(family, fontsize=18, y=1.0)
                fig.legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5, 0.95), ncol=len(intensities) + 1, frameon=True)
                plt.tight_layout(rect=[0, 0, 1, 0.95])
            else:
                fig.legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5, 1.0), ncol=len(intensities) + 1, frameon=True)
                plt.tight_layout(rect=[0, 0, 1, 0.95])

        file_suffix = "" if family is None else f"_{family.replace(' ', '_')}"
        out_path = f'{base_out_path}{file_suffix}'
        plt.savefig(f'{out_path}.pdf', bbox_inches='tight', pad_inches=0.0)
        plt.savefig(f'{out_path}.png', dpi=300, bbox_inches='tight', pad_inches=0.0)
        print(f"Plots saved successfully to {out_path}.pdf and .png")
        plt.close(fig)

def plot_scope_all_systems(args):
    system_dfs = []
    out_dir = f'plots/output/{args.environment}/{args.workload}/scope'
    os.makedirs(f'plots/data/{args.environment}/{args.workload}/scope', exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)

    for system in VALID_SYSTEMS:
        df = load_scope_df(system, args)
        if df is None:
            print(f"Skipping {system}: no data found")
        else:
            print(f"Loaded data for {system} with {len(df)} rows")
            if not args.no_extraction:
                df.to_csv(f'plots/data/{args.environment}/{args.workload}/scope/scope_{system}.csv', index=False)
        system_dfs.append((system, df))

    families_to_plot = [None] + list(TXN_FAMILY_MAP.keys())
    
    for family in families_to_plot:
        n_systems = len(system_dfs)
        figsize_y = 6 if family is None else 6.5
        sharey_mode = False if args.separate_crdb_y else 'row'
        fig, axes = plt.subplots(2, n_systems, figsize=(4 * n_systems, figsize_y), sharex='col', sharey=sharey_mode)

        if n_systems == 1:
            axes = axes.reshape(2, 1)

        for col, (system, df) in enumerate(system_dfs):
            ax_top = axes[0, col]
            ax_bottom = axes[1, col]

            if df is not None:
                intensities = sorted(df['intensity'].unique())
                for intensity in intensities:
                    sub_df = df[df['intensity'] == intensity]
                    
                    if family is None:
                        tp_col = 'throughput'
                        p50_col = 'latency_p50'
                        p95_col = 'latency_p95'
                    else:
                        fam_clean = family.replace(" ", "_")
                        tp_col = f'throughput_{fam_clean}'
                        p50_col = f'latency_p50_{fam_clean}'
                        p95_col = f'latency_p95_{fam_clean}'
                        
                    sub_df = sub_df[sub_df[tp_col] > 0]
                    if sub_df.empty:
                        continue

                    line, = ax_top.plot(sub_df['geo_dist'], sub_df[tp_col], linewidth=2, label=f'{intensity}')
                    color = line.get_color()
                    
                    valid_lat = sub_df[sub_df[p50_col] > 0]
                    if not valid_lat.empty:
                        ax_bottom.plot(valid_lat['geo_dist'], valid_lat[p50_col], color=color, linewidth=2, label=f'{intensity}')
                        ax_bottom.plot(valid_lat['geo_dist'], valid_lat[p95_col], color=color, linestyle='--', linewidth=2, alpha=0.7)

            else:
                ax_top.text(0.5, 0.5, "no data", transform=ax_top.transAxes, ha='center', va='center', color='gray')
                ax_bottom.text(0.5, 0.5, "no data", transform=ax_bottom.transAxes, ha='center', va='center', color='gray')

            sys_display = SYSTEM_DISPLAY_NAME_MAP.get(system, system)
            
            ax_top.set_title(f'Throughput ({sys_display})')
            if col == 0:
                ax_top.set_ylabel('Throughput\n(txns/s)')
            elif args.separate_crdb_y and system == 'crdb':
                ax_top.set_ylabel('Throughput\nCRDB', rotation=270, labelpad=40)
                ax_top.yaxis.set_label_position("right")
                ax_top.yaxis.tick_right()
            
            ax_top.grid(True, linestyle='--', alpha=0.7)
            if not args.separate_crdb_y or system != 'crdb':
                ax_top.yaxis.set_major_formatter(ticker.FuncFormatter(lambda y, _: f"{y/1000:g}k"))
                ax_top.set_xlim(*X_AXIS_LIMITS)
                max_y = THROUGHPUT_YLIM[1] if family is None else THROUGHPUT_YLIM[1] / 4
                ax_top.set_ylim(bottom=0, top=max_y)
            else:
                ax_top.yaxis.set_major_formatter(ticker.FuncFormatter(lambda y, _: f"{int(y)}"))
                ax_top.set_xlim(*X_AXIS_LIMITS)
                ax_top.set_ylim(bottom=THROUGHPUT_YLIM_CRDB[0], top=None)

            ax_bottom.set_title(f'Latency ({sys_display})')
            if col == 0:
                ax_bottom.set_ylabel('Latency\n(ms)')
            elif args.separate_crdb_y and system == 'crdb':
                ax_bottom.set_ylabel('Latency\nCRDB', rotation=270, labelpad=40)
                ax_bottom.yaxis.set_label_position("right")
                ax_bottom.yaxis.tick_right()

            ax_bottom.grid(True, linestyle='--', alpha=0.7)
            ax_bottom.set_yscale('log')
            ax_bottom.set_xlim(*X_AXIS_LIMITS)
            ax_bottom.set_xlabel('Geo-distribution (%)')
            
            if not args.separate_crdb_y or system != 'crdb':
                ax_bottom.set_ylim(bottom=LATENCY_YLIM[0], top=LATENCY_YLIM[1])
            else:
                ax_bottom.set_ylim(bottom=LATENCY_YLIM_CRDB[0], top=None)

            if args.separate_crdb_y and col > 0 and system != 'crdb':
                ax_top.set_yticklabels([])
                ax_bottom.set_yticklabels([])

        all_handles = []
        all_labels = []
        for ax in axes[0, :]:
            handles, labels = ax.get_legend_handles_labels()
            for h, l in zip(handles, labels):
                if l not in all_labels:
                    all_handles.append(h)
                    all_labels.append(l)

        if all_handles:
            import matplotlib.lines as mlines
            all_handles.insert(0, mlines.Line2D([], [], color='none'))
            all_labels.insert(0, 'Keys:')
            if family is not None:
                fig.suptitle(family, fontsize=20, y=1.0)
                legend = fig.legend(
                    all_handles,
                    all_labels,
                    loc='upper center',
                    bbox_to_anchor=(0.5, 0.94),
                    ncol=len(all_labels),
                    frameon=True,
                    fontsize=14
                )
                plt.tight_layout(rect=[0, 0, 1, 0.95])
            else:
                legend = fig.legend(
                    all_handles,
                    all_labels,
                    loc='upper center',
                    bbox_to_anchor=(0.5, 1.0),
                    ncol=len(all_labels),
                    frameon=True,
                    fontsize=14
                )
                plt.tight_layout(rect=[0, 0, 1, 0.95])
        
        file_suffix = "all" if family is None else f"all_{family.replace(' ', '_')}"
        out_path = f'{out_dir}/scope_{file_suffix}'
        plt.savefig(f'{out_path}.pdf', bbox_inches='tight', pad_inches=0.0)
        plt.savefig(f'{out_path}.png', dpi=300, bbox_inches='tight', pad_inches=0.0)
        print(f"Plots saved successfully to {out_path}.pdf and .png")
        plt.close(fig)
        
    print("Generating txn families latency comparison (Intensity=2)...")
    n_systems = len(system_dfs)
    sharey_mode = False if args.separate_crdb_y else True
    fig, axes = plt.subplots(1, n_systems, figsize=(4 * n_systems, 3.5), sharey=sharey_mode)
    if n_systems == 1:
        axes = [axes]

    crdb_col = None
    for col, (system, df) in enumerate(system_dfs):
        if args.separate_crdb_y and system == 'crdb':
            crdb_col = col
            break

    for col, (system, df) in enumerate(system_dfs):
        ax = axes[col]
        sys_display = SYSTEM_DISPLAY_NAME_MAP.get(system, system)
        subplot_y_values = []
        
        if df is not None:
            sub_df = df[df['intensity'] == 2]
            for fam in TXN_FAMILY_MAP.keys():
                fam_clean = fam.replace(" ", "_")
                p50_col = f'latency_p50_{fam_clean}'
                p95_col = f'latency_p95_{fam_clean}'
                
                if p50_col in sub_df.columns and p95_col in sub_df.columns:
                    valid_df = sub_df[sub_df[p50_col] > 0]
                    if not valid_df.empty:
                        line, = ax.plot(valid_df['geo_dist'], valid_df[p50_col], linewidth=2, label=fam)
                        subplot_y_values.extend(line.get_ydata())
                        color = line.get_color()
                        p95_line = ax.plot(valid_df['geo_dist'], valid_df[p95_col], color=color, linestyle='--', linewidth=2, alpha=0.7)
                        subplot_y_values.extend(p95_line[0].get_ydata())
        else:
            ax.text(0.5, 0.5, "no data", transform=ax.transAxes, ha='center', va='center', color='gray')

        ax.set_title(f'{sys_display}')
        if col == 0:
            ax.set_ylabel('Latency (ms)')
        elif args.separate_crdb_y and system == 'crdb':
            ax.set_ylabel('Latency\nCRDB', rotation=270, labelpad=40)
            ax.yaxis.set_label_position("right")
            ax.yaxis.tick_right()
        ax.set_xlabel('Geo-distribution (%)')
        ax.grid(True, linestyle='--', alpha=0.7)
        ax.set_yscale('log')
        if not args.separate_crdb_y or system != 'crdb':
            ax.set_ylim(bottom=LATENCY_YLIM_FAMILY[0], top=LATENCY_YLIM_FAMILY[1])
        else:
            valid_y = [y for y in subplot_y_values if y is not None and np.isfinite(y) and y > 0]
            if valid_y:
                top = max(valid_y) * 1.05
                ax.set_ylim(bottom=LATENCY_YLIM_CRDB[0], top=top)
            else:
                ax.set_ylim(bottom=LATENCY_YLIM_CRDB[0], top=LATENCY_YLIM_FAMILY[1])
        ax.set_xlim(*X_AXIS_LIMITS)

        if args.separate_crdb_y and col > 0 and system != 'crdb':
            ax.set_yticklabels([])

    all_handles = []
    all_labels = []
    for ax in axes:
        handles, labels = ax.get_legend_handles_labels()
        for h, l in zip(handles, labels):
            if l not in all_labels:
                all_handles.append(h)
                all_labels.append(l)

    if all_handles:
        fig.legend(
            all_handles,
            all_labels,
            loc='upper center',
            bbox_to_anchor=(0.5, 1.1),
            ncol=min(len(all_labels), 5),
            frameon=True,
            fontsize=14
        )

    out_path = f'{out_dir}/scope_txn_families_int2'
    plt.savefig(f'{out_path}.pdf', bbox_inches='tight', pad_inches=0.0)
    plt.savefig(f'{out_path}.png', dpi=300, bbox_inches='tight', pad_inches=0.0)
    print(f"Plots saved successfully to {out_path}.pdf and .png")
    plt.close(fig)

    print("Done")

def extract_and_plot(args):
    if args.system == 'all':
        plot_scope_all_systems(args)
        return

    df = load_scope_df(args.system, args)
    if df is None:
        print(f"No valid data found to plot for {args.system}.")
        return

    out_dir = f'plots/output/{args.environment}/{args.workload}/scope'
    os.makedirs(out_dir, exist_ok=True)
    out_path = f'{out_dir}/scope_{args.system.lower()}'
    plot_scope_system(df, args.system, args, out_path)

if __name__ == "__main__":
    args = parse_args()
    extract_and_plot(args)
