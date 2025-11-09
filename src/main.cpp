#include <iostream>      // For standard input/output operations (std::cout, std::cerr)
#include <csignal>       // For handling system signals like SIGINT (Ctrl+C)
#include <cstdlib>       // For environment variable access and general utilities
#include <thread>        // For using std::this_thread and std::sleep_for
#include <chrono>        // For specifying time durations (e.g., std::chrono::seconds)
#include "server.hpp"    // Custom header that defines the KVServer class
#include "database.hpp"  // Custom header that defines the Database class

// Global pointer to the KVServer instance, so that it can be accessed inside signal handlers.
KVServer* g_server = nullptr;

// ------------------------------------------------------------
// Signal handler function for graceful server shutdown
// ------------------------------------------------------------
void signalHandler(int signal) {
    // Print which signal was received (e.g., SIGINT or SIGTERM)
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    
    // If the global server object exists, stop it gracefully
    if (g_server) {
        g_server->stop();
    }
    
    // Exit the program cleanly
    exit(0);
}

// ------------------------------------------------------------
// Helper function: Retrieves an environment variable by name.
// If not found, returns the provided default value.
// ------------------------------------------------------------
std::string getEnv(const char* name, const std::string& default_value) {
    // Attempt to fetch environment variable
    const char* value = std::getenv(name);
    
    // If the variable exists, return its value as std::string.
    // Otherwise, return the default value.
    return value ? std::string(value) : default_value;
}

// ------------------------------------------------------------
// Main entry point of the program
// ------------------------------------------------------------
int main(int argc, char* argv[]) {
    // ------------------------------
    // Register signal handlers
    // ------------------------------
    // Attach signalHandler to handle Ctrl+C (SIGINT) and termination (SIGTERM).
    // This ensures that when the user interrupts the program, the server shuts down gracefully.
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // ------------------------------
    // Read configuration
    // ------------------------------
    // The following environment variables determine how the server and database are configured.
    // If they are not set, default values are used.
    std::string db_host = getEnv("DB_HOST", "localhost");     // Database host address
    std::string db_port = getEnv("DB_PORT", "5432");          // PostgreSQL port
    std::string db_name = getEnv("DB_NAME", "kvstore");       // Database name
    std::string db_user = getEnv("DB_USER", "kvuser");        // Database username
    std::string db_password = getEnv("DB_PASSWORD", "kvpass");// Database password
    
    // Read server configuration (convert from string to int/size_t)
    int server_port = std::stoi(getEnv("SERVER_PORT", "8080"));           // Port on which the KV server will listen
    size_t cache_size = std::stoul(getEnv("CACHE_SIZE", "1000"));         // Cache capacity for in-memory key-value storage
    size_t thread_pool_size = std::stoul(getEnv("THREAD_POOL_SIZE", "8"));// Number of worker threads for handling requests
    
    // ------------------------------
    // Display the loaded configuration
    // ------------------------------
    std::cout << "=== KV Server Configuration ===" << std::endl;
    std::cout << "Database Host: " << db_host << std::endl;
    std::cout << "Database Port: " << db_port << std::endl;
    std::cout << "Database Name: " << db_name << std::endl;
    std::cout << "Server Port: " << server_port << std::endl;
    std::cout << "Cache Size: " << cache_size << std::endl;
    std::cout << "Thread Pool Size: " << thread_pool_size << std::endl;
    std::cout << "================================\n" << std::endl;
    
    // ------------------------------
    // Wait briefly before connecting
    // ------------------------------
    // This delay allows time for the PostgreSQL server to fully start up
    // (useful when both services start simultaneously, such as in Docker environments).
    std::cout << "Waiting for database connection..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // ------------------------------
    // Initialize the database connection
    // ------------------------------
    // Create a Database object using the configured parameters.
    Database* db = new Database(db_host, db_port, db_name, db_user, db_password);
    
    // If the connection failed, display an error and exit the program.
    if (!db->isConnected()) {
        std::cerr << "Failed to connect to database. Exiting." << std::endl;
        delete db; // Free allocated memory for Database object
        return 1;  // Exit with non-zero status to indicate failure
    }
    
    // ------------------------------
    // Initialize and start the server
    // ------------------------------
    // Create the KVServer object with the configured parameters.
    // The server uses the given database connection for persistence.
    // g_server = new KVServer(server_port, cache_size, thread_pool_size, db);
    g_server = new KVServer(server_port, cache_size, thread_pool_size,
                            db_host, db_port, db_name, db_user, db_password);
    
    // Attempt to start the server.
    if (!g_server->start()) {
        std::cerr << "Failed to start server. Exiting." << std::endl;
        delete g_server; // Clean up if startup failed
        return 1;
    }
    
    // If we reach here, the server is up and running.
    std::cout << "Server is running. Press Ctrl+C to stop." << std::endl;
    
    // ------------------------------
    // Keep the main thread alive
    // ------------------------------
    // The server likely runs its own internal threads to handle requests.
    // This infinite loop prevents the main process from exiting,
    // allowing the server to continue running until interrupted.
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // The code below will never be reached because of the infinite loop.
    // The program terminates only when the signal handler is triggered.
    return 0;
}
