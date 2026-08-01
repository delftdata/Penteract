import os
import pandas as pd
import numpy as np

for test_type in ['scope', 'dependent']:
    base_dir = f"plots/raw_data/local/benchx/{test_type}/CRDB"
    os.makedirs(base_dir, exist_ok=True)
    for folder in ['0_0.0_200000_10_200', '10', '20']: # some dummy folders
        client_dir = os.path.join(base_dir, folder, "client", "0-0")
        os.makedirs(client_dir, exist_ok=True)
        # Fake transactions.csv
        data = {
            'code': np.random.choice([0, 1, 2, 3, 4], 100),
            'sent_at': np.linspace(1000, 2000, 100) * 1e6,
            'received_at': np.linspace(1010, 2015, 100) * 1e6
        }
        pd.DataFrame(data).to_csv(os.path.join(client_dir, "transactions.csv"), index=False)
        pd.DataFrame([{'elapsed_time': 1e9, 'committed': 100}]).to_csv(os.path.join(base_dir, folder, "summary.csv"), index=False)
        
print("Mock data generated. Running extraction...")
os.system("python3 plots/scope_comparison.py -e local -sy crdb")
os.system("python3 plots/dependent_comparison.py -e local -sy crdb")
