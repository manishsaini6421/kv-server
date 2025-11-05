#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/**
 * @brief Multi-threaded load generator for KV Server
 * 
 * This program simulates multiple concurrent clients sending HTTP requests
 * to a Key-Value (KV) server to test its performance under load.
 * 
 * It supports different workload patterns:
 *  - PUT_ALL: All requests are POST (insert/update operations)
 *  - GET_ALL: All requests are GET (read operations)
 *  - GET_POPULAR: Repeated reads on a small set of keys (tests cache)
 *  - MIXED: Random mix of GET, PUT, and DELETE operations
 */

enum WorkloadType {
    PUT_ALL,       // Only PUT operations
    GET_ALL,       // Only GET operations
    GET_POPULAR,   // Frequent reads on small key subset
    MIXED          // Combination of GET, PUT, and DELETE
};

// Structure to hold per-thread (client) statistics
struct ClientStats {
    uint64_t requests_sent = 0;        // Total number of requests sent
    uint64_t requests_succeeded = 0;   // Requests that received HTTP 200 OK
    uint64_t requests_failed = 0;      // Requests that failed (non-200 response or timeout)
    uint64_t total_latency_ms = 0;     // Cumulative latency in milliseconds
};

// Global atomic flag to indicate if test is running
std::atomic<bool> g_running(true);

// Vector to store statistics for each client thread
std::vector<ClientStats> g_client_stats;

/**
 * @brief Sends a raw HTTP request to the target server and returns the response.
 * 
 * This function creates a TCP socket, connects to the specified host and port,
 * sends the HTTP request string, and waits for a response.
 * 
 * @param host Target server hostname or IP
 * @param port Target server port number
 * @param request Full HTTP request string
 * @return std::string Response received from the server
 */
std::string sendHttpRequest(const std::string& host, int port, const std::string& request) {
    // Create a TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return "";
    }

    // Configure server address structure
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // Initialize to zeros
    server_addr.sin_family = AF_INET;             // IPv4 address family
    server_addr.sin_port = htons(port);           // Convert port to network byte order
    inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr); // Convert IP string to binary form

    // Attempt to connect to the server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sock);
        return "";
    }

    // Send the HTTP request string to the server
    send(sock, request.c_str(), request.length(), 0);

    // Buffer for receiving response
    char buffer[4096];
    ssize_t bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0); // Receive response data
    close(sock); // Close socket after receiving data

    // Null-terminate and return response if data received
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        return std::string(buffer);
    }
    
    // Return empty string if no data was received
    return "";
}

/**
 * @brief Function executed by each client thread.
 * 
 * Generates requests according to the chosen workload type and measures
 * response latency and success rates.
 */
void clientThread(int thread_id, const std::string& host, int port, 
                  WorkloadType workload, int duration_sec, int key_space_size) {
    // Initialize random number generators for key and operation selection
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> key_dist(1, key_space_size);
    std::uniform_int_distribution<> popular_key_dist(1, 10); // For GET_POPULAR (small key set)
    std::uniform_int_distribution<> op_dist(0, 2);           // Random op selection (0=GET, 1=PUT, 2=DELETE)

    // Reference to current thread's statistics object
    ClientStats& stats = g_client_stats[thread_id];

    // Calculate the time at which the thread should stop sending requests
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(duration_sec);

    // Loop until duration ends or global stop signal is set
    while (g_running && std::chrono::steady_clock::now() < end_time) {
        std::string request;
        std::ostringstream body;
        auto req_start = std::chrono::steady_clock::now(); // Record start time

        int key;
        std::string value;

        // Select request type and format request body depending on workload type
        switch (workload) {
            // -----------------------------
            case PUT_ALL: {
                // Generate a random key and corresponding value
                key = key_dist(gen);
                value = "value_" + std::to_string(key) + "_" + std::to_string(thread_id);
                body << "{\"key\":\"key_" << key << "\",\"value\":\"" << value << "\"}";

                // Create HTTP POST request to /api/kv
                request = "POST /api/kv HTTP/1.1\r\n";
                request += "Host: " + host + "\r\n";
                request += "Content-Type: application/json\r\n";
                request += "Content-Length: " + std::to_string(body.str().length()) + "\r\n";
                request += "\r\n" + body.str();
                break;
            }

            // -----------------------------
            case GET_ALL: {
                // Generate random GET request for a random key
                key = key_dist(gen);
                request = "GET /api/kv?key=key_" + std::to_string(key) + " HTTP/1.1\r\n";
                request += "Host: " + host + "\r\n\r\n";
                break;
            }

            // -----------------------------
            case GET_POPULAR: {
                // Generate GET request for a popular key (small subset of keys)
                key = popular_key_dist(gen);
                request = "GET /api/kv?key=popular_key_" + std::to_string(key) + " HTTP/1.1\r\n";
                request += "Host: " + host + "\r\n\r\n";
                break;
            }

            // -----------------------------
            case MIXED: {
                // Randomly choose an operation
                int op = op_dist(gen);
                key = key_dist(gen);

                if (op == 0) { // GET request
                    request = "GET /api/kv?key=key_" + std::to_string(key) + " HTTP/1.1\r\n";
                    request += "Host: " + host + "\r\n\r\n";
                } else if (op == 1) { // PUT request
                    value = "value_" + std::to_string(key);
                    body << "{\"key\":\"key_" << key << "\",\"value\":\"" << value << "\"}";
                    request = "POST /api/kv HTTP/1.1\r\n";
                    request += "Host: " + host + "\r\n";
                    request += "Content-Type: application/json\r\n";
                    request += "Content-Length: " + std::to_string(body.str().length()) + "\r\n";
                    request += "\r\n" + body.str();
                } else { // DELETE request
                    request = "DELETE /api/kv?key=key_" + std::to_string(key) + " HTTP/1.1\r\n";
                    request += "Host: " + host + "\r\n\r\n";
                }
                break;
            }
        }

        // Send the request and capture the response
        std::string response = sendHttpRequest(host, port, request);

        // Measure request latency
        auto req_end = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(req_end - req_start).count();

        // Update thread statistics
        stats.requests_sent++;
        if (!response.empty() && response.find("200 OK") != std::string::npos) {
            stats.requests_succeeded++;
            stats.total_latency_ms += latency;
        } else {
            stats.requests_failed++;
        }
    }
}

/**
 * @brief Prints command-line usage information.
 */
void printUsage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " <host> <port> <workload> <num_threads> <duration_sec> [key_space_size]" << std::endl;
    std::cout << "Workload types: PUT_ALL, GET_ALL, GET_POPULAR, MIXED" << std::endl;
    std::cout << "Example: " << prog_name << " localhost 8080 GET_POPULAR 10 60 10000" << std::endl;
}

int main(int argc, char* argv[]) {
    // Ensure minimum required arguments are provided
    if (argc < 6) {
        printUsage(argv[0]);
        return 1;
    }

    // Parse command-line arguments
    std::string host = argv[1];
    int port = std::stoi(argv[2]);
    std::string workload_str = argv[3];
    int num_threads = std::stoi(argv[4]);
    int duration_sec = std::stoi(argv[5]);
    int key_space_size = (argc > 6) ? std::stoi(argv[6]) : 10000;

    // Map string to workload type enum
    WorkloadType workload;
    if (workload_str == "PUT_ALL") workload = PUT_ALL;
    else if (workload_str == "GET_ALL") workload = GET_ALL;
    else if (workload_str == "GET_POPULAR") workload = GET_POPULAR;
    else if (workload_str == "MIXED") workload = MIXED;
    else {
        std::cerr << "Invalid workload type: " << workload_str << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    // Display configuration summary
    std::cout << "=== Load Generator Configuration ===" << std::endl;
    std::cout << "Target: " << host << ":" << port << std::endl;
    std::cout << "Workload: " << workload_str << std::endl;
    std::cout << "Threads: " << num_threads << std::endl;
    std::cout << "Duration: " << duration_sec << " seconds" << std::endl;
    std::cout << "Key Space Size: " << key_space_size << std::endl;
    std::cout << "====================================\n" << std::endl;

    // Preload popular keys into server for GET_POPULAR workload
    if (workload == GET_POPULAR) {
        std::cout << "Pre-populating popular keys..." << std::endl;
        for (int i = 1; i <= 10; i++) {
            std::ostringstream body;
            body << "{\"key\":\"popular_key_" << i << "\",\"value\":\"popular_value_" << i << "\"}";
            std::string request = "POST /api/kv HTTP/1.1\r\n";
            request += "Host: " + host + "\r\n";
            request += "Content-Type: application/json\r\n";
            request += "Content-Length: " + std::to_string(body.str().length()) + "\r\n";
            request += "\r\n" + body.str();
            sendHttpRequest(host, port, request);
        }
        std::cout << "Pre-population complete.\n" << std::endl;
    }

    // Prepare statistics and thread containers
    g_client_stats.resize(num_threads);
    std::vector<std::thread> threads;

    std::cout << "Starting load test..." << std::endl;
    auto test_start = std::chrono::steady_clock::now();

    // Launch multiple client threads
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(clientThread, i, host, port, workload, duration_sec, key_space_size);
    }

    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }

    auto test_end = std::chrono::steady_clock::now();
    auto actual_duration = std::chrono::duration_cast<std::chrono::seconds>(test_end - test_start).count();

    // Aggregate statistics from all threads
    uint64_t total_sent = 0, total_succeeded = 0, total_failed = 0, total_latency = 0;
    for (const auto& stats : g_client_stats) {
        total_sent += stats.requests_sent;
        total_succeeded += stats.requests_succeeded;
        total_failed += stats.requests_failed;
        total_latency += stats.total_latency_ms;
    }

    // Print performance summary
    std::cout << "\n=== Load Test Results ===" << std::endl;
    std::cout << "Actual Duration: " << actual_duration << " seconds" << std::endl;
    std::cout << "Total Requests Sent: " << total_sent << std::endl;
    std::cout << "Successful Requests: " << total_succeeded << std::endl;
    std::cout << "Failed Requests: " << total_failed << std::endl;
    std::cout << "Success Rate: " << (total_sent > 0 ? (double)total_succeeded / total_sent * 100.0 : 0) << "%" << std::endl;
    std::cout << "\n--- Performance Metrics ---" << std::endl;
    std::cout << "Average Throughput: " << (actual_duration > 0 ? (double)total_succeeded / actual_duration : 0) << " req/sec" << std::endl;
    std::cout << "Average Response Time: " << (total_succeeded > 0 ? (double)total_latency / total_succeeded : 0) << " ms" << std::endl;
    std::cout << "=========================\n" << std::endl;

    return 0;
}
