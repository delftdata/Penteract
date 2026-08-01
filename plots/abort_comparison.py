from re import A
import os
import re
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import argparse

plt.rcParams.update({
    'font.size': 14,
    'axes.titlesize': 16,
    'axes.labelsize': 14,
    'xtick.labelsize': 14,
    'ytick.labelsize': 14,
    'legend.fontsize': 14
})

VALID_SYSTEMS = ['calvin', 'slog', 'Detock', 'janus', 'crdb']
SYSTEM_DISPLAY_NAME_MAP = {
    'calvin': 'Calvin',
    'slog': 'SLOG',
    'Detock': 'Detock',
    'janus': 'Janus',
    'crdb': 'CRDB'
}
ABORT_YLIM = 40

def extract_aborts_from_logs(raw_logs_dir):
    total_commits = 0
    total_aborts = 0
    total_tps = 0.0
    
    if not os.path.isdir(raw_logs_dir):
        return total_commits, total_aborts, total_tps
        
    for f in os.listdir(raw_logs_dir):
        if "benchmark_container_" in f and f.endswith(".log"):
            last_line = None
            tps_val = 0.0
            crdb_commits = None
            crdb_aborts = None
            crdb_duration = 60.0 # Default fallback
            
            with open(os.path.join(raw_logs_dir, f), "r") as log_file:
                for line in log_file:
                    if "benchmark.cpp:" in line and "S:" in line and "C:" in line and "A:" in line:
                        last_line = line
                    elif "Avg. TPS:" in line:
                        try:
                            tps_val = float(line.split("Avg. TPS:")[1].strip())
                        except Exception:
                            pass
                    elif "Total Committed:" in line:
                        try:
                            crdb_commits = int(line.split("Total Committed:")[1].strip())
                        except Exception:
                            pass
                    elif "Total Aborted:" in line:
                        try:
                            crdb_aborts = int(line.split("Total Aborted:")[1].strip())
                        except Exception:
                            pass
                    elif "Running for" in line and "seconds" in line:
                        try:
                            # Extract duration from "Running for X seconds..."
                            match = re.search(r'Running for (\d+) seconds', line)
                            if match:
                                crdb_duration = float(match.group(1))
                        except Exception:
                            pass
            
            if crdb_commits is not None or crdb_aborts is not None:
                commits = crdb_commits if crdb_commits is not None else 0
                aborts = crdb_aborts if crdb_aborts is not None else 0
                total_commits += commits
                total_aborts += aborts
                if tps_val == 0.0:
                    tps_val = commits / crdb_duration

            elif last_line:
                # E.g., S: 241 (355373); C: 609 (293589); A: 131 (61784); R: 0 (0)
                try:
                    c_match = re.search(r'C:\s*\d+\s*\((\d+)\)', last_line)
                    a_match = re.search(r'A:\s*\d+\s*\((\d+)\)', last_line)
                    if c_match and a_match:
                        total_commits += int(c_match.group(1))
                        total_aborts += int(a_match.group(1))
                except Exception as e:
                    print(f"Error parsing line {last_line}: {e}")
                    
            total_tps += tps_val
                    
    return total_commits, total_aborts, total_tps

def load_dependent_aborts(args):
    data = []
    for system in VALID_SYSTEMS:
        sys_display = SYSTEM_DISPLAY_NAME_MAP.get(system, system)
        base_dir = f"plots/raw_data/{args.environment}/{args.workload}/dependent/{sys_display}"
        if not os.path.exists(base_dir):
            continue

        for folder in os.listdir(base_dir):
            folder_path = os.path.join(base_dir, folder)
            if not os.path.isdir(folder_path):
                continue
            
            try:
                dep_percent = int(folder)
            except ValueError:
                continue
                
            raw_logs_dir = os.path.join(folder_path, "raw_logs")
            commits, aborts, tps = extract_aborts_from_logs(raw_logs_dir)
            
            abort_rate = 0.0
            if (commits + aborts) > 0:
                abort_rate = round((aborts / (commits + aborts)) * 100.0, 3)
                
            data.append({
                'system': system,
                'dependent_percent': dep_percent,
                'abort_rate': abort_rate,
                'throughput': round(tps, 3)
            })
            
    if not data:
        return None
    
    df = pd.DataFrame(data)
    return df

def load_scope_aborts(args, target_intensity=2):
    data = []
    for system in VALID_SYSTEMS:
        sys_display = SYSTEM_DISPLAY_NAME_MAP.get(system, system)
        base_dir = f"plots/raw_data/{args.environment}/{args.workload}/scope/{sys_display}"
        if not os.path.exists(base_dir):
            continue

        for folder in os.listdir(base_dir):
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
                
            if intensity != target_intensity:
                continue
                
            geo_dist = int(round((1.0 - base_x) * 100))
                
            raw_logs_dir = os.path.join(folder_path, "raw_logs")
            commits, aborts, tps = extract_aborts_from_logs(raw_logs_dir)
            
            abort_rate = 0.0
            if (commits + aborts) > 0:
                abort_rate = round((aborts / (commits + aborts)) * 100.0, 3)
                
            data.append({
                'system': system,
                'geo_dist': geo_dist,
                'abort_rate': abort_rate,
                'throughput': round(tps, 3)
            })
            
    if not data:
        return None
        
    df = pd.DataFrame(data)
    return df

def load_delete_ablation_aborts(args):
    data = []
    for system in VALID_SYSTEMS:
        sys_display = SYSTEM_DISPLAY_NAME_MAP.get(system, system)
        base_dir = f"plots/raw_data/{args.environment}/{args.workload}/delete_ablation/{sys_display}"
        if not os.path.exists(base_dir):
            continue

        for folder in os.listdir(base_dir):
            folder_path = os.path.join(base_dir, folder)
            if not os.path.isdir(folder_path):
                continue
            
            try:
                delete_percent = int(folder)
            except ValueError:
                continue
                
            raw_logs_dir = os.path.join(folder_path, "raw_logs")
            commits, aborts, tps = extract_aborts_from_logs(raw_logs_dir)
            
            abort_rate = 0.0
            if (commits + aborts) > 0:
                abort_rate = round((aborts / (commits + aborts)) * 100.0, 3)
                
            data.append({
                'system': system,
                'delete_percent': delete_percent,
                'abort_rate': abort_rate,
                'throughput': round(tps, 3)
            })
            
    if not data:
        return None
    
    df = pd.DataFrame(data)
    return df

def extract_and_plot(args):
    dep_df = load_dependent_aborts(args)
    scope_df = load_scope_aborts(args, target_intensity=2)
    delete_df = load_delete_ablation_aborts(args)
    plotted_values = []
    
    if args.plot_throughput:
        fig, axes = plt.subplots(2, 3, figsize=(8, 5), sharey='row')
        abort_axes = axes[0]
        tp_axes = axes[1]
    else:
        fig, axes = plt.subplots(1, 3, figsize=(8, 3), sharey=True)
        abort_axes = axes
        tp_axes = None
        
    crdb_abort_axes = [ax.twinx() for ax in abort_axes] if args.separate_crdb_y else abort_axes
    if tp_axes is not None:
        crdb_tp_axes = [ax.twinx() for ax in tp_axes] if args.separate_crdb_y else tp_axes
    else:
        crdb_tp_axes = None
        
    def get_color(system):
        return plt.rcParams['axes.prop_cycle'].by_key()['color'][VALID_SYSTEMS.index(system) % len(plt.rcParams['axes.prop_cycle'].by_key()['color'])]

    if dep_df is not None and not dep_df.empty:
        for system in VALID_SYSTEMS:
            sys_df = dep_df[dep_df['system'] == system].sort_values('dependent_percent')
            if not sys_df.empty:
                ax_to_use = crdb_abort_axes[0] if system == 'crdb' and args.separate_crdb_y else abort_axes[0]
                lw = 2 if system == 'crdb' else 1
                ax_to_use.plot(sys_df['dependent_percent'], sys_df['abort_rate'], linewidth=lw, label=SYSTEM_DISPLAY_NAME_MAP.get(system, system), color=get_color(system))
                plotted_values.extend({
                    'scenario': 'dependent',
                    'system': row.system,
                    'x_value': row.dependent_percent,
                    'abort_rate': row.abort_rate,
                    'throughput': row.throughput,
                } for row in sys_df.itertuples())
    
    abort_axes[0].set_title('Dependent\ntransactions')
    abort_axes[0].set_xlabel('Dependent Txn (%)' if not args.plot_throughput else '')
    abort_axes[0].set_ylabel('Abort Rate (%)')
    abort_axes[0].set_xlim(0, 100)
    abort_axes[0].set_ylim(0, ABORT_YLIM)
    abort_axes[0].grid(True, linestyle='--', alpha=0.7)
    abort_axes[0].axvline(x=50, color='grey', linestyle=':', linewidth=1.5)
    abort_axes[0].text(49, 0.58, 'Default', transform=abort_axes[0].get_xaxis_transform(), rotation=90, color='grey', verticalalignment='bottom', horizontalalignment='right', fontsize=12)
    
    if delete_df is not None and not delete_df.empty:
        for system in VALID_SYSTEMS:
            sys_df = delete_df[delete_df['system'] == system].sort_values('delete_percent')
            if not sys_df.empty:
                ax_to_use = crdb_abort_axes[1] if system == 'crdb' and args.separate_crdb_y else abort_axes[1]
                lw = 2 if system == 'crdb' else 1
                ax_to_use.plot(sys_df['delete_percent'], sys_df['abort_rate'], linewidth=lw, label=SYSTEM_DISPLAY_NAME_MAP.get(system, system), color=get_color(system))
                plotted_values.extend({
                    'scenario': 'delete_ablation',
                    'system': row.system,
                    'x_value': row.delete_percent,
                    'abort_rate': row.abort_rate,
                    'throughput': row.throughput,
                } for row in sys_df.itertuples())

    abort_axes[1].set_title('Delete\ntransactions')
    abort_axes[1].set_xlabel('Delete Txn (%)' if not args.plot_throughput else '')
    abort_axes[1].set_xlim(0, 100)
    abort_axes[1].set_ylim(0, ABORT_YLIM)
    abort_axes[1].grid(True, linestyle='--', alpha=0.7)
    abort_axes[1].axvline(x=20, color='grey', linestyle=':', linewidth=1.5)
    abort_axes[1].text(19, 0.58, 'Default', transform=abort_axes[1].get_xaxis_transform(), rotation=90, color='grey', verticalalignment='bottom', horizontalalignment='right', fontsize=12)
    
    if scope_df is not None and not scope_df.empty:
        for system in VALID_SYSTEMS:
            sys_df = scope_df[scope_df['system'] == system].sort_values('geo_dist')
            if not sys_df.empty:
                ax_to_use = crdb_abort_axes[2] if system == 'crdb' and args.separate_crdb_y else abort_axes[2]
                lw = 2 if system == 'crdb' else 1
                ax_to_use.plot(sys_df['geo_dist'], sys_df['abort_rate'], linewidth=lw, label=SYSTEM_DISPLAY_NAME_MAP.get(system, system), color=get_color(system))
                plotted_values.extend({
                    'scenario': 'scope',
                    'system': row.system,
                    'x_value': row.geo_dist,
                    'abort_rate': row.abort_rate,
                    'throughput': row.throughput,
                } for row in sys_df.itertuples())
                
    abort_axes[2].set_title('Geo-distribution')
    abort_axes[2].set_xlabel('Geo-distribution (%)' if not args.plot_throughput else '')
    abort_axes[2].set_xlim(0, 100)
    abort_axes[2].set_ylim(0, ABORT_YLIM)
    abort_axes[2].grid(True, linestyle='--', alpha=0.7)
    abort_axes[2].axvline(x=50, color='grey', linestyle=':', linewidth=1.5)
    abort_axes[2].text(49, 0.58, 'Default', transform=abort_axes[2].get_xaxis_transform(), rotation=90, color='grey', verticalalignment='bottom', horizontalalignment='right', fontsize=12)
    
    if args.separate_crdb_y:
        for ax in crdb_abort_axes:
            ax.set_ylim(bottom=0)
        crdb_abort_axes[-1].set_ylabel('Abort Rate\nCRDB (%)', rotation=270, labelpad=40)
    
    if tp_axes is not None:
        if dep_df is not None and not dep_df.empty:
            for system in VALID_SYSTEMS:
                sys_df = dep_df[dep_df['system'] == system].sort_values('dependent_percent')
                if not sys_df.empty:
                    ax_to_use = crdb_tp_axes[0] if system == 'crdb' and args.separate_crdb_y else tp_axes[0]
                    lw = 2 if system == 'crdb' else 1
                    ax_to_use.plot(sys_df['dependent_percent'], sys_df['throughput'], linewidth=lw, label=SYSTEM_DISPLAY_NAME_MAP.get(system, system), color=get_color(system))
        tp_axes[0].set_xlabel('Dependent Txn (%)')
        tp_axes[0].set_ylabel('Throughput\n(txns/s)')
        tp_axes[0].set_xlim(0, 100)
        tp_axes[0].set_ylim(bottom=0)
        tp_axes[0].grid(True, linestyle='--', alpha=0.7)
        tp_axes[0].axvline(x=50, color='grey', linestyle=':', linewidth=1.5)
        tp_axes[0].text(49, 0.58, 'Default', transform=tp_axes[0].get_xaxis_transform(), rotation=90, color='grey', verticalalignment='bottom', horizontalalignment='right', fontsize=12)
        
        if delete_df is not None and not delete_df.empty:
            for system in VALID_SYSTEMS:
                sys_df = delete_df[delete_df['system'] == system].sort_values('delete_percent')
                if not sys_df.empty:
                    ax_to_use = crdb_tp_axes[1] if system == 'crdb' and args.separate_crdb_y else tp_axes[1]
                    lw = 2 if system == 'crdb' else 1
                    ax_to_use.plot(sys_df['delete_percent'], sys_df['throughput'], linewidth=lw, label=SYSTEM_DISPLAY_NAME_MAP.get(system, system), color=get_color(system))
        tp_axes[1].set_xlabel('Delete Txn (%)')
        tp_axes[1].set_xlim(0, 100)
        tp_axes[1].set_ylim(bottom=0)
        tp_axes[1].grid(True, linestyle='--', alpha=0.7)
        tp_axes[1].axvline(x=20, color='grey', linestyle=':', linewidth=1.5)
        tp_axes[1].text(19, 0.58, 'Default', transform=tp_axes[1].get_xaxis_transform(), rotation=90, color='grey', verticalalignment='bottom', horizontalalignment='right', fontsize=12)

        if scope_df is not None and not scope_df.empty:
            for system in VALID_SYSTEMS:
                sys_df = scope_df[scope_df['system'] == system].sort_values('geo_dist')
                if not sys_df.empty:
                    ax_to_use = crdb_tp_axes[2] if system == 'crdb' and args.separate_crdb_y else tp_axes[2]
                    lw = 2 if system == 'crdb' else 1
                    ax_to_use.plot(sys_df['geo_dist'], sys_df['throughput'], linewidth=lw, label=SYSTEM_DISPLAY_NAME_MAP.get(system, system), color=get_color(system))
        tp_axes[2].set_xlabel('Geo-distribution (%)')
        tp_axes[2].set_xlim(0, 100)
        tp_axes[2].set_ylim(bottom=0)
        tp_axes[2].grid(True, linestyle='--', alpha=0.7)
        tp_axes[2].axvline(x=50, color='grey', linestyle=':', linewidth=1.5)
        tp_axes[2].text(49, 0.58, 'Default', transform=tp_axes[2].get_xaxis_transform(), rotation=90, color='grey', verticalalignment='bottom', horizontalalignment='right', fontsize=12)
        
        if args.separate_crdb_y:
            for ax in crdb_tp_axes:
                ax.set_ylim(bottom=0)
            crdb_tp_axes[-1].set_ylabel('Throughput\nCRDB', rotation=270, labelpad=40)

    all_handles = []
    all_labels = []
    for ax_list in ([abort_axes, crdb_abort_axes] if args.separate_crdb_y else [abort_axes]):
        for ax in ax_list:
            handles, labels = ax.get_legend_handles_labels()
            for h, l in zip(handles, labels):
                if l not in all_labels:
                    all_handles.append(h)
                    all_labels.append(l)
                
    if all_handles:
        fig.legend(all_handles, all_labels, loc='upper center', bbox_to_anchor=(0.5, 1.05 if args.plot_throughput else 1.1), ncol=len(all_labels), frameon=True)
    
    plt.tight_layout()
    
    out_dir = f'plots/output/{args.environment}/{args.workload}/aborts'
    os.makedirs(out_dir, exist_ok=True)
    out_path = f'{out_dir}/abort_rates'
    
    plt.savefig(f'{out_path}.pdf', bbox_inches='tight', pad_inches=0.0)
    plt.savefig(f'{out_path}.png', dpi=300, bbox_inches='tight', pad_inches=0.0)
    
    data_dir = f'plots/data/{args.environment}/{args.workload}/aborts'
    os.makedirs(data_dir, exist_ok=True)
    
    if dep_df is not None and not dep_df.empty:
        dep_df.sort_values(by=['system', 'dependent_percent']).to_csv(f'{data_dir}/dependent_aborts.csv', index=False)
    if scope_df is not None and not scope_df.empty:
        scope_df.sort_values(by=['system', 'geo_dist']).to_csv(f'{data_dir}/scope_aborts.csv', index=False)
    if delete_df is not None and not delete_df.empty:
        delete_df.sort_values(by=['system', 'delete_percent']).to_csv(f'{data_dir}/delete_ablation_aborts.csv', index=False)

    plotted_values_df = pd.DataFrame(plotted_values)
    plotted_values_df.to_csv(f'{data_dir}/plotted_values.csv', index=False)
        
    print(f"Plots saved to {out_dir}/ and data saved to {data_dir}/")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Extract and plot abort rates.")
    parser.add_argument("-w",  "--workload", default='benchx', help="Workload to run")
    parser.add_argument("-e",  "--environment", default='st', help="Environment")
    parser.add_argument("-pt", "--plot_throughput", default=False, action="store_true", help="Whether to also plot throughput")
    parser.add_argument("-cy", "--separate_crdb_y", action="store_true", default=False, help="Whether to use a separate y-axis for CRDB to allow it to auto-scale.")
    args = parser.parse_args()
    
    extract_and_plot(args)
