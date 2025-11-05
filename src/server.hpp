#pragma once

#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include "cache.hpp"
#include "database.hpp"

/**
 * @brief HTTP-based KV Server with caching and database backend
 * 
 * This class implements a Key-Value (KV) Store server that supports
 * HTTP-based requests (REST API style). It provides CRUD operations
 * using a combination of:
 *  - In-memory LRU cache for fast access
 *  - PostgreSQL (or similar) database for persistence
 * 
 * Supported REST endpoints:
 *  - POST /api/kv            → Create or update a key-value pair
 *  - GET /api/kv?key=<key>   → Retrieve the value of a given key
 *  - DELETE /api/kv?key=<key>→ Delete a key-value pair
 */
class KVServer {
private:
    // Pointer to LRU cache for storing recently accessed key-value pairs in memory
    std::unique_ptr<LRUCache> cache;

    // Pointer to database backend (PostgreSQL or similar) for persistent storage
    std::unique_ptr<Database> database;
    
    // File descriptor for the server socket that listens for incoming client connections
    int server_socket;

    // Port number on which the server listens for HTTP requests
    int port;

    // Number of worker threads to handle client requests concurrently
    size_t thread_pool_size;

    // Pool of worker threads that process incoming HTTP requests
    std::vector<std::thread> worker_threads;

    // Atomic flag indicating whether the server is currently running
    std::atomic<bool> running;
    
    // ===== Server Statistics =====

    // Counts the number of times a requested key was found in the cache
    std::atomic<uint64_t> cache_hits;

    // Counts the number of times a requested key was not found in the cache
    std::atomic<uint64_t> cache_misses;

    // Tracks total number of HTTP requests received by the server
    std::atomic<uint64_t> total_requests;
    
    /**
     * @brief Handles an individual client connection.
     * 
     * Reads the HTTP request, determines its type (GET, POST, DELETE),
     * processes it accordingly, and sends back an appropriate HTTP response.
     * 
     * @param client_socket Socket file descriptor for the connected client.
     */
    void handleClient(int client_socket);

    /**
     * @brief Function executed by each worker thread.
     * 
     * Continuously listens for new client connections, retrieves requests from
     * the socket, and delegates them to the `handleClient()` function.
     */
    void workerThread();
    
    /**
     * @brief Handles HTTP PUT/POST requests (Create or Update operation).
     * 
     * Parses the key-value pair from the request body and stores it in both
     * cache and database. Returns a success or error message in HTTP format.
     * 
     * @param body The HTTP request body containing the key-value data.
     * @return A formatted HTTP response string.
     */
    std::string handlePutRequest(const std::string& body);

    /**
     * @brief Handles HTTP GET requests (Read operation).
     * 
     * Extracts the key from the query string, checks the cache first for the value.
     * If not found, retrieves it from the database, updates the cache, and
     * returns the value in the HTTP response.
     * 
     * @param query The URL query string containing the key parameter.
     * @return A formatted HTTP response with the key’s value or an error message.
     */
    std::string handleGetRequest(const std::string& query);

    /**
     * @brief Handles HTTP DELETE requests (Delete operation).
     * 
     * Parses the key from the query string and removes it from both cache
     * and database. Returns a success or failure HTTP response.
     * 
     * @param query The URL query string containing the key parameter.
     * @return A formatted HTTP response indicating success or failure.
     */
    std::string handleDeleteRequest(const std::string& query);
    
    /**
     * @brief Extracts the "key" parameter from an HTTP query string.
     * 
     * Example:
     *   Input:  "key=example"
     *   Output: "example"
     * 
     * @param query The HTTP query string.
     * @return Extracted key as a string.
     */
    std::string parseKeyFromQuery(const std::string& query);

    /**
     * @brief Parses the key and value from a request body (used in POST/PUT).
     * 
     * The body typically contains data in the format:
     *   key=<key>&value=<value>
     * 
     * This function extracts both components for further processing.
     * 
     * @param body The HTTP request body.
     * @param key Reference to store the extracted key.
     * @param value Reference to store the extracted value.
     */
    void parseKeyValue(const std::string& body, std::string& key, std::string& value);
    
    /**
     * @brief Builds a complete HTTP response string.
     * 
     * Constructs the standard HTTP response format with:
     *  - Status line (e.g., HTTP/1.1 200 OK)
     *  - Headers (e.g., Content-Type, Content-Length)
     *  - Body (e.g., the response message or data)
     * 
     * @param status_code The HTTP status code (e.g., 200, 404).
     * @param body The response body text.
     * @return A complete HTTP response string.
     */
    std::string buildHttpResponse(int status_code, const std::string& body);

    /**
     * @brief Returns a human-readable status message for a given HTTP code.
     * 
     * Example:
     *  - 200 → "OK"
     *  - 404 → "Not Found"
     *  - 500 → "Internal Server Error"
     * 
     * @param status_code HTTP status code.
     * @return Corresponding status text string.
     */
    std::string getStatusText(int status_code);

public:
    /**
     * @brief Constructs the KVServer with specified configuration.
     * 
     * @param port Port number for the server to listen on.
     * @param cache_size Maximum number of entries to hold in the cache.
     * @param thread_pool_size Number of worker threads to spawn.
     * @param db Pointer to an already initialized Database object.
     */
    KVServer(int port, size_t cache_size, size_t thread_pool_size, Database* db);

    /**
     * @brief Destructor that ensures resources (threads, sockets) are cleaned up.
     */
    ~KVServer();
    
    /**
     * @brief Starts the server and begins accepting HTTP connections.
     * 
     * Initializes the socket, spawns worker threads, and sets the `running` flag.
     * 
     * @return true if the server started successfully, false otherwise.
     */
    bool start();

    /**
     * @brief Gracefully stops the server.
     * 
     * Closes sockets, signals threads to stop, and waits for them to finish.
     */
    void stop();
    
    /**
     * @brief Prints runtime statistics about cache and request performance.
     * 
     * Displays the number of cache hits, misses, and total handled requests.
     */
    void printStats();
};
