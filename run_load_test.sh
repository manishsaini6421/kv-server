#!/bin/bash

# Configurations
HOST="localhost"
PORT="8080"
DURATIONS=300      # test duration in seconds
KEY_SPACE_SIZE=10000

LOADS=("GET_POPULAR" "GET_ALL" "PUT_ALL" "MIXED")
THREAD_COUNTS=(5 10 15 20 25 30 35 40 45 50 55 60 65 70 75 80 85 90 95 100)

OUTPUT_DIR="./load_test_results"
mkdir -p $OUTPUT_DIR

# CSV Header
echo "Workload,Threads,Duration,TotalRequests,SuccessfulRequests,FailedRequests,SuccessRate,AvgThroughput,AvgResponseTime" > $OUTPUT_DIR/summary.csv

for workload in "${LOADS[@]}"; do
  echo "Starting tests for workload: $workload"

  for threads in "${THREAD_COUNTS[@]}"; do
    echo "Running load generator: workload=$workload, threads=$threads"

    # Run load generator and capture entire output
    output=$(taskset -c 1,2,3,5,6,7 ./build/load_generator $HOST $PORT $workload $threads $DURATIONS)

    # Optional: Save full raw output to file per run
    echo "$output" > "$OUTPUT_DIR/${workload}_${threads}_raw.log"

    # Parse fields from output using grep and awk
    total_requests=$(echo "$output" | grep "Total Requests Sent:" | awk '{print $4}')
    successful_requests=$(echo "$output" | grep "Successful Requests:" | awk '{print $3}')
    failed_requests=$(echo "$output" | grep "Failed Requests:" | awk '{print $3}')
    success_rate=$(echo "$output" | grep "Success Rate:" | awk '{print $3}' | tr -d '%')
    avg_throughput=$(echo "$output" | grep "Average Throughput:" | awk '{print $3}')
    avg_resp_time=$(echo "$output" | grep "Average Response Time:" | awk '{print $4}')

    # Save summary line to CSV
    echo "$workload,$threads,$DURATIONS,$total_requests,$successful_requests,$failed_requests,$success_rate,$avg_throughput,$avg_resp_time" >> $OUTPUT_DIR/summary.csv

    echo "Completed: $workload $threads threads; Throughput = $avg_throughput req/sec; Avg Latency = $avg_resp_time ms"
  done
done

echo "All load tests complete. Summary saved to $OUTPUT_DIR/summary.csv"
