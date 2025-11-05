#include "server.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// =======================
// Constructor Definition
// =======================
KVServer::KVServer(int port, size_t cache_size, size_t thread_pool_size, Database *db)
    : port(port), thread_pool_size(thread_pool_size), running(false),
      cache_hits(0), cache_misses(0), total_requests(0)
{
    // Initialize cache with given size
    cache = std::make_unique<LRUCache>(cache_size);
    // Take ownership of the provided database pointer
    database = std::unique_ptr<Database>(db);

    // Initialize server socket to an invalid state
    server_socket = -1;
}

// =======================
// Destructor
// =======================
KVServer::~KVServer()
{
    // Ensure server stops and cleans up resources on destruction
    stop();
}

// =======================
// Start the server
// =======================
bool KVServer::start()
{
    // Create a TCP socket (IPv4, Stream type)
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }

    // Allow socket address reuse (helps during quick restarts)
    // Enable the SO_REUSEADDR socket option on the server socket.
    // This option allows the server to reuse the same local address (IP + port)
    // immediately after the program is restarted, without waiting for the operating
    // system to release it from the TIME_WAIT state. Without this option, attempting
    // to bind() to the same port soon after restarting the server may cause the error
    // "Address already in use". Setting opt = 1 enables this behavior.
    // Parameters:
    //   server_socket - the socket file descriptor on which the option is being set
    //   SOL_SOCKET    - specifies that the option is at the socket level
    //   SO_REUSEADDR  - the option that allows reusing the local address
    //   &opt          - pointer to the integer value (1 to enable the option)
    //   sizeof(opt)   - size of the option value being passed
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Prepare server address structure
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // Zero-initialize
    server_addr.sin_family = AF_INET;             // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY;     // Bind to all network interfaces
    server_addr.sin_port = htons(port);           // Set port in network byte order

    // Bind the socket to the specified IP and port
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "Failed to bind socket to port " << port << std::endl;
        close(server_socket);
        return false;
    }

    // Start listening for incoming connections (max 128 in queue)
    if (listen(server_socket, 128) < 0)
    {
        std::cerr << "Failed to listen on socket" << std::endl;
        close(server_socket);
        return false;
    }

    running = true;
    std::cout << "KV Server listening on port " << port << std::endl;

    // Spawn worker threads to handle client connections concurrently
    // Create and launch multiple worker threads for the server's thread pool.
    // Each iteration of the loop creates one new thread that runs the member function
    // KVServer::workerThread() — the function responsible for processing incoming
    // client requests or tasks from a shared queue.
    //
    // Explanation of components:
    //   - thread_pool_size: The total number of worker threads to create, typically
    //     defined based on the number of CPU cores or expected workload.
    //   - i: Loop counter used to create each thread sequentially.
    //   - worker_threads: A vector (std::vector<std::thread>) that stores all the
    //     thread objects so that the server can later join or manage them properly.
    //   - emplace_back(...): Constructs the thread directly in place within the vector,
    //     avoiding unnecessary copies.
    //   - &KVServer::workerThread: Pointer to the member function that each thread will execute.
    //   - this: Pointer to the current KVServer instance, passed so that each thread
    //     has access to the server’s data members and methods.
    //
    // In summary, this loop initializes the server’s thread pool by spawning
    // 'thread_pool_size' background worker threads that run concurrently to handle
    // client requests efficiently.
    for (size_t i = 0; i < thread_pool_size; ++i)
    {
        worker_threads.emplace_back(&KVServer::workerThread, this);
    }

    return true;
}

// =======================
// Worker Thread Function
// =======================
void KVServer::workerThread()
{
    while (running)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        // Accept an incoming client connection request on the server socket.
        // This function blocks (waits) until a client attempts to connect to the server.
        // When a connection request arrives, the `accept()` call creates a new socket
        // dedicated to communicating with that specific client and returns its file
        // descriptor (`client_socket`).
        //
        // Explanation of parameters:
        //   - server_socket: The listening socket that was previously created, bound to
        //     an address, and set to listen for incoming connections using listen().
        //   - (struct sockaddr *)&client_addr: A pointer to a sockaddr structure where
        //     the details (IP address and port number) of the connecting client will be stored.
        //   - &client_len: A pointer to a variable that initially contains the size of
        //     the client_addr structure. On return, it holds the actual size of the
        //     address information stored.
        //
        // Return value:
        //   - On success: Returns a new socket descriptor (`client_socket`) that represents
        //     the connection to the newly accepted client. The server uses this socket
        //     to send and receive data with that specific client.
        //   - On failure: Returns -1, and `errno` is set to indicate the specific error.
        //
        // In summary, this line accepts a new client connection from the listening socket
        // and establishes a separate communication channel with that client.
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

        if (client_socket < 0)
        {
            if (running)
            {
                std::cerr << "Failed to accept connection" << std::endl;
            }
            continue;
        }

        // Handle client request synchronously in the same thread
        handleClient(client_socket);

        // Close client connection after serving
        close(client_socket);
    }
}

// =======================
// Handle HTTP Request
// =======================
void KVServer::handleClient(int client_socket)
{
    char buffer[4096];

    // Receive HTTP request data from client
    ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0)
    {
        return; // Client disconnected or read failed
    }

    buffer[bytes_read] = '\0'; // Null-terminate received data
    std::string request(buffer);

    total_requests++; // Increment total request count

    // Parse the first line of HTTP request: METHOD PATH VERSION
    std::istringstream iss(request);
    std::string method, path, version;
    iss >> method >> path >> version;

    std::string response;

    // Extract query string from the URL if present
    std::string query;
    size_t query_pos = path.find('?');
    if (query_pos != std::string::npos)
    {
        query = path.substr(query_pos + 1);
        path = path.substr(0, query_pos);
    }

    // Extract request body (everything after "\r\n\r\n")
    std::string body;
    size_t body_pos = request.find("\r\n\r\n");
    if (body_pos != std::string::npos)
    {
        body = request.substr(body_pos + 4);
    }

    // Route the request to appropriate handler
    if (path == "/api/kv")
    {
        if (method == "POST")
        {
            response = handlePutRequest(body); // Create/Update
        }
        else if (method == "GET")
        {
            response = handleGetRequest(query); // Read
        }
        else if (method == "DELETE")
        {
            response = handleDeleteRequest(query); // Delete
        }
        else
        {
            response = buildHttpResponse(405, "{\"error\":\"Method not allowed\"}");
        }
    }
    // Endpoint to check server statistics
    else if (path == "/stats")
    {
        std::ostringstream stats;
        stats << "{\"total_requests\":" << total_requests
              << ",\"cache_hits\":" << cache_hits
              << ",\"cache_misses\":" << cache_misses
              << ",\"hit_rate\":" << (total_requests > 0 ? (double)cache_hits / total_requests : 0)
              << "}";
        response = buildHttpResponse(200, stats.str());
    }
    // Invalid endpoint
    else
    {
        response = buildHttpResponse(404, "{\"error\":\"Not found\"}");
    }

    // Send back the HTTP response
    send(client_socket, response.c_str(), response.length(), 0);
}

// =======================
// Handle POST/PUT Request
// =======================
std::string KVServer::handlePutRequest(const std::string &body)
{
    std::string key, value;
    parseKeyValue(body, key, value); // Extract key and value from JSON

    if (key.empty())
    {
        return buildHttpResponse(400, "{\"error\":\"Invalid request body\"}");
    }

    // Write key-value pair to database
    if (!database->put(key, value))
    {
        return buildHttpResponse(500, "{\"error\":\"Database write failed\"}");
    }

    // Update in-memory cache as well
    cache->put(key, value);

    return buildHttpResponse(200, "{\"status\":\"success\"}");
}

// =======================
// Handle GET Request
// =======================
std::string KVServer::handleGetRequest(const std::string &query)
{
    std::string key = parseKeyFromQuery(query);

    if (key.empty())
    {
        return buildHttpResponse(400, "{\"error\":\"Missing key parameter\"}");
    }

    // Try to get value from cache first
    std::string value = cache->get(key);
    if (!value.empty())
    {
        cache_hits++;
        std::ostringstream json;
        json << "{\"key\":\"" << key << "\",\"value\":\"" << value << "\"}";
        return buildHttpResponse(200, json.str());
    }

    cache_misses++;

    // If cache miss, retrieve from database
    if (database->get(key, value))
    {
        cache->put(key, value); // Store result in cache for next time

        std::ostringstream json;
        json << "{\"key\":\"" << key << "\",\"value\":\"" << value << "\"}";
        return buildHttpResponse(200, json.str());
    }

    // Key not found in database
    return buildHttpResponse(404, "{\"error\":\"Key not found\"}");
}

// =======================
// Handle DELETE Request
// =======================
std::string KVServer::handleDeleteRequest(const std::string &query)
{
    std::string key = parseKeyFromQuery(query);

    if (key.empty())
    {
        return buildHttpResponse(400, "{\"error\":\"Missing key parameter\"}");
    }

    // Remove from database and cache
    database->del(key);
    cache->del(key);

    return buildHttpResponse(200, "{\"status\":\"success\"}");
}

// =======================
// Parse key from query string
// =======================
std::string KVServer::parseKeyFromQuery(const std::string &query)
{
    size_t key_pos = query.find("key=");
    if (key_pos == std::string::npos)
    {
        return "";
    }

    size_t start = key_pos + 4;
    size_t end = query.find('&', start);

    if (end == std::string::npos)
    {
        return query.substr(start);
    }

    return query.substr(start, end - start);
}

// =======================
// Parse key-value from JSON body
// =======================
void KVServer::parseKeyValue(const std::string &body, std::string &key, std::string &value)
{
    // Simplified JSON parsing for {"key":"...", "value":"..."}
    size_t key_start = body.find("\"key\"");
    size_t value_start = body.find("\"value\"");

    if (key_start == std::string::npos || value_start == std::string::npos)
    {
        return;
    }

    // Extract key substring
    size_t key_value_start = body.find(':', key_start) + 1;
    key_value_start = body.find('"', key_value_start) + 1;
    size_t key_value_end = body.find('"', key_value_start);
    key = body.substr(key_value_start, key_value_end - key_value_start);

    // Extract value substring
    size_t value_value_start = body.find(':', value_start) + 1;
    value_value_start = body.find('"', value_value_start) + 1;
    size_t value_value_end = body.find('"', value_value_start);
    value = body.substr(value_value_start, value_value_end - value_value_start);
}

// =======================
// Build HTTP Response
// =======================
std::string KVServer::buildHttpResponse(int status_code, const std::string &body)
{
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << " " << getStatusText(status_code) << "\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << body;
    return response.str();
}

// =======================
// Return Text for HTTP Status Code
// =======================
std::string KVServer::getStatusText(int status_code)
{
    switch (status_code)
    {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 500:
        return "Internal Server Error";
    default:
        return "Unknown";
    }
}

// =======================
// Stop the server gracefully
// =======================
void KVServer::stop()
{
    if (!running)
        return; // Do nothing if already stopped

    running = false; // Signal worker threads to stop

    // Close server socket to stop accepting new connections
    if (server_socket >= 0)
    {
        close(server_socket);
        server_socket = -1;
    }

    // Join all worker threads before exiting
    for (auto &thread : worker_threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    // Clear thread vector
    worker_threads.clear();

    // Print server statistics before shutting down
    printStats();
}

// =======================
// Display runtime statistics
// =======================
void KVServer::printStats()
{
    std::cout << "\n=== Server Statistics ===" << std::endl;
    std::cout << "Total Requests: " << total_requests << std::endl;
    std::cout << "Cache Hits: " << cache_hits << std::endl;
    std::cout << "Cache Misses: " << cache_misses << std::endl;
    if (total_requests > 0)
    {
        double hit_rate = (double)cache_hits / total_requests * 100.0;
        std::cout << "Cache Hit Rate: " << hit_rate << "%" << std::endl;
    }
}
