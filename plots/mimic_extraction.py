import os
import tarfile
import json
import hashlib
import pandas as pd
import numpy as np
import argparse

VALID_ENVIRONMENTS = ['local', 'st', 'aws']
VALID_SYSTEMS = ['calvin', 'slog', 'Detock', 'janus']
SYSTEM_DISPLAY_NAME_MAP = {
    'calvin': 'Calvin',
    'slog': 'SLOG',
    'Detock': 'Detock',
    'janus': 'Janus'
}

CACHE_FILENAME_TEMPLATE = 'mimic_{system}_{benchmark}.meta.json'

def compute_raw_data_fingerprint_for_folders(base_dir, folders):
    h = hashlib.sha256()
    for folder in sorted(folders):
        folder_dir = os.path.join(base_dir, folder)
        for root, dirs, files in os.walk(folder_dir):
            files.sort()
            for fname in files:
                file_path = os.path.join(root, fname)
                try:
                    stat = os.stat(file_path)
                except OSError:
                    continue
                rel_path = os.path.relpath(file_path, base_dir)
                h.update(rel_path.encode('utf-8'))
                h.update(str(stat.st_size).encode('utf-8'))
                h.update(str(stat.st_mtime_ns).encode('utf-8'))
    return h.hexdigest()

def load_cached_data(cache_path):
    if not os.path.exists(cache_path):
        return None, None
    try:
        with open(cache_path, 'r') as f:
            content = json.load(f)
            data = content.get('data')
            df = pd.DataFrame(data) if data else None
            return content.get('fingerprint'), df
    except Exception:
        return None, None

def save_cached_data(cache_path, fingerprint, df):
    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    data = df.to_dict(orient='records') if df is not None else []
    with open(cache_path, 'w') as f:
        json.dump({'fingerprint': fingerprint, 'data': data}, f)

def parse_args():
    parser = argparse.ArgumentParser(description="Extract experiment results for the mimic scenario.")
    parser.add_argument("-w",  "--workload", default='benchx', help="Workload to run (default: benchx)")
    parser.add_argument("-e",  "--environment", default='st', choices=VALID_ENVIRONMENTS, help="What type of machine the experiment was run on.")
    parser.add_argument("-ne", "--no_extraction", action="store_true", default=False, help="Whether to skip extraction and just load from CSV")
    return parser.parse_args()

def extract_system_mimic_data(system, args):
    print(f"Loading data for {system}...")
    data_dir = f'plots/data/{args.environment}/{args.workload}/mimic/raw'
    sys_display = SYSTEM_DISPLAY_NAME_MAP.get(system, system)
    base_dir = f"plots/raw_data/{args.environment}/{args.workload}/mimic/{sys_display}"
    
    if not os.path.exists(base_dir):
        return None

    benchmarks_folders = {}
    for folder in os.listdir(base_dir):
        if not os.path.isdir(os.path.join(base_dir, folder)):
            continue
        parts = folder.split('_')
        if len(parts) < 2:
            continue
        benchmark = '_'.join(parts[:-1])
        if benchmark not in benchmarks_folders:
            benchmarks_folders[benchmark] = []
        benchmarks_folders[benchmark].append(folder)

    benchmark_dfs = []
    for benchmark, folders in benchmarks_folders.items():
        cache_path = os.path.join(data_dir, CACHE_FILENAME_TEMPLATE.format(system=system, benchmark=benchmark))
        cached_fp, cached_df = load_cached_data(cache_path)

        if args.no_extraction:
            if cached_df is not None:
                benchmark_dfs.append(cached_df)
            else:
                print(f"Skipping {system} {benchmark}: no cached data found at {cache_path}")
            continue

        current_fp = compute_raw_data_fingerprint_for_folders(base_dir, folders)
        if cached_fp == current_fp and cached_df is not None:
            print(f"Skipping extraction for {system} {benchmark}: raw data unchanged")
            benchmark_dfs.append(cached_df)
            continue

        print(f"Extracting data for {system} {benchmark}...")
        data = []
        for folder in sorted(folders):
            folder_path = os.path.join(base_dir, folder)
            parts = folder.split('_')
            try:
                x_val = int(float(parts[-1]))
            except ValueError:
                continue

            print(f"Processing folder: {folder} (Benchmark: {benchmark}, x_val: {x_val})")
            
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
            total_fused_count = 0
            total_unfused_count = 0
            client_dir = os.path.join(folder_path, "client")
            if os.path.isdir(client_dir):
                for c in os.listdir(client_dir):
                    c_path = os.path.join(client_dir, c)
                    df = None
                    if os.path.isdir(c_path):
                        tx_file = os.path.join(c_path, "transactions.csv")
                        if os.path.exists(tx_file):
                            with pd.option_context('mode.chained_assignment', None):
                                df = pd.read_csv(tx_file, on_bad_lines='skip', low_memory=False)
                    elif c.endswith(".tar.gz"):
                        try:
                            with tarfile.open(c_path, "r:gz") as tar:
                                for member in tar.getmembers():
                                    if member.name.endswith("transactions.csv"):
                                        f = tar.extractfile(member)
                                        with pd.option_context('mode.chained_assignment', None):
                                            df = pd.read_csv(f, on_bad_lines='skip', low_memory=False)
                        except Exception as e:
                            print(f"Failed to read tar {c_path}: {e}")
                            
                    if df is not None and 'received_at' in df.columns and 'sent_at' in df.columns:
                        df['received_at'] = pd.to_numeric(df['received_at'], errors='coerce')
                        df['sent_at'] = pd.to_numeric(df['sent_at'], errors='coerce')
                        
                        if 'code' in df.columns:
                            df['dep_id'] = df['code'].astype(str).str.extract(r'(dep_\d+)')[0]
                            df['dep_id'] = df['dep_id'].fillna(df['txn_id'].astype(str))
                            
                            total_unfused_count += len(df)
                            
                            grouped = df.groupby('dep_id').agg(
                                min_sent=('sent_at', 'min'),
                                max_received=('received_at', 'max')
                            )
                            grouped['duration'] = grouped['max_received'] - grouped['min_sent']
                            
                            total_fused_count += len(grouped)
                            
                            valid_df = grouped.dropna(subset=['duration'])
                            latencies.extend(valid_df['duration'].tolist())
                        else:
                            df['duration'] = df['received_at'] - df['sent_at']
                            valid_df = df.dropna(subset=['duration'])
                            latencies.extend(valid_df['duration'].tolist())
                            
                            total_unfused_count += len(df)
                            total_fused_count += len(df)

            if total_unfused_count > 0:
                throughput = throughput * (total_fused_count / total_unfused_count)

            p50_latency = np.percentile(latencies, 50) / 1_000_000.0 if latencies else -1
            p95_latency = np.percentile(latencies, 95) / 1_000_000.0 if latencies else -1
            p99_latency = np.percentile(latencies, 99) / 1_000_000.0 if latencies else -1

            data_point = {
                'benchmark': benchmark,
                'x_val': x_val,
                f'{system}_throughput': round(throughput),
                f'{system}_p50': round(p50_latency) if p50_latency != -1 else -1,
                f'{system}_p95': round(p95_latency) if p95_latency != -1 else -1,
                f'{system}_p99': round(p99_latency) if p99_latency != -1 else -1
            }
            data.append(data_point)

        if data:
            df = pd.DataFrame(data)
            os.makedirs(data_dir, exist_ok=True)
            save_cached_data(cache_path, current_fp, df)
            benchmark_dfs.append(df)
            print(f"Saved fingerprint cache for {system} {benchmark}")

    if benchmark_dfs:
        return pd.concat(benchmark_dfs, ignore_index=True)
    return None

def extract_mimic_data(args):
    all_system_dfs = []
    
    for system in VALID_SYSTEMS:
        sys_df = extract_system_mimic_data(system, args)
        if sys_df is not None and not sys_df.empty:
            all_system_dfs.append(sys_df)
            
    if not all_system_dfs:
        print("No mimic data found for any system.")
        return
        
    benchmarks = set()
    for df in all_system_dfs:
        benchmarks.update(df['benchmark'].unique())
        
    out_dir = f'plots/data/{args.environment}/{args.workload}/mimic'
    os.makedirs(out_dir, exist_ok=True)
    
    for benchmark in benchmarks:
        print(f"\nProcessing benchmark: {benchmark}")
        merged_df = None
        for df in all_system_dfs:
            bench_df = df[df['benchmark'] == benchmark].drop(columns=['benchmark'])
            if bench_df.empty:
                continue
                
            if merged_df is None:
                merged_df = bench_df
            else:
                merged_df = pd.merge(merged_df, bench_df, on='x_val', how='outer')
                
        if merged_df is not None:
            merged_df = merged_df.sort_values('x_val')
            merged_df = merged_df.fillna(-1)
            
            for col in merged_df.columns:
                if col != 'x_val':
                    merged_df[col] = merged_df[col].astype('Int64')
                    
            ordered_cols = ['x_val']
            for sys in VALID_SYSTEMS:
                for metric in ['throughput', 'p50', 'p95', 'p99']:
                    col_name = f'{sys}_{metric}'
                    if col_name in merged_df.columns:
                        ordered_cols.append(col_name)
                        
            merged_df = merged_df[ordered_cols]
                    
            out_path = os.path.join(out_dir, f'mimic_{benchmark}.csv')
            merged_df.to_csv(out_path, index=False)
            print(f"Saved {out_path}")

if __name__ == "__main__":
    args = parse_args()
    extract_mimic_data(args)
