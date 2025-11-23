import matplotlib.pyplot as plt
import pandas as pd

# Load CSV file
df = pd.read_csv('./load_test_results/summary.csv')

# Workloads to plot
workloads = df['Workload'].unique()

# Create plots directory
import os
os.makedirs('./load_test_results/plots', exist_ok=True)

# Plot config
plt.style.use('seaborn-darkgrid')
colors = ['tab:blue', 'tab:orange', 'tab:green', 'tab:red']

# Plot Throughput vs Threads for each workload
plt.figure(figsize=(10, 6))
for i, workload in enumerate(workloads):
    wdf = df[df['Workload'] == workload]
    plt.plot(wdf['Threads'], wdf['AvgThroughput'], marker='o', label=workload, color=colors[i])
plt.xlabel('Number of Clients (Threads)')
plt.ylabel('Average Throughput (requests/sec)')
plt.title('Number of Clients vs Throughput')
plt.legend()
plt.savefig('./load_test_results/plots/clients_vs_throughput.png')
plt.close()

# Plot Avg Response Time vs Threads for each workload
plt.figure(figsize=(10, 6))
for i, workload in enumerate(workloads):
    wdf = df[df['Workload'] == workload]
    plt.plot(wdf['Threads'], wdf['AvgResponseTime'], marker='o', label=workload, color=colors[i])
plt.xlabel('Number of Clients (Threads)')
plt.ylabel('Average Response Time (ms)')
plt.title('Number of Clients vs Average Response Time')
plt.legend()
plt.savefig('./load_test_results/plots/clients_vs_response_time.png')
plt.close()

print("Plots saved to ./load_test_results/plots/")
