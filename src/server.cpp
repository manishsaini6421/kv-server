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
KVServer::KVServer(int port, size_t cache_size, size_t thread_pool_size,
                   const std::string &db_host, const std::string &db_port,
                   const std::string &db_name, const std::string &db_user,
                   const std::string &db_password)
    : port(port), thread_pool_size(thread_pool_size),
      db_host(db_host), db_port(db_port), db_name(db_name),
      db_user(db_user), db_password(db_password), running(false),
      cache_hits(0), cache_misses(0), total_requests(0)
{
    // Initialize cache with given size
    cache = std::make_unique<LRUCache>(cache_size);
    // Take ownership of the provided database pointer
    // database = std::unique_ptr<Database>(db);

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
    // Create a unique_ptr Database instance per thread
    auto database = std::make_unique<Database>(
        db_host.c_str(), db_port.c_str(), db_name.c_str(), 
        db_user.c_str(), db_password.c_str());

   

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
        handleClient(client_socket, database.get());

        // Close client connection after serving
        close(client_socket);
    }
}

// =======================
// Handle HTTP Request
// =======================
void KVServer::handleClient(int client_socket, Database* database)
{
    char buffer[4096];

    // Receive data from the connected client socket and store it in the buffer.
    // Parameters:
    //  - client_socket: The socket descriptor representing the client's connection.
    //  - buffer: The character array where received data will be stored.
    //  - sizeof(buffer) - 1: The maximum number of bytes to read. We subtract 1 to leave
    //    room for a null terminator ('\0') in case we later treat the buffer as a C string.
    //  - 0: Flags argument; 0 means default behavior (blocking receive).
    //
    // Return value:
    //  - recv() returns the number of bytes actually received and stored in 'buffer'.
    //  - A return value of 0 means the peer has performed an orderly shutdown
    //    (connection closed).
    //  - A return value of -1 indicates an error (errno will be set accordingly).
    ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0)
    {
        return; // Client disconnected or read failed
    }
    // received data is not null-terminated by default so we do it manually
    buffer[bytes_read] = '\0'; // Null-terminate received data
    // Convert buffer to std::string for easier manipulation
    std::string request(buffer);

    total_requests++; // Increment total request count

    // Parse the first line of an HTTP request to extract the method, path, and HTTP version.
    //
    // Example of a typical HTTP request line:
    //     "GET /index.html HTTP/1.1"
    //
    // 1. std::istringstream iss(request);
    //    - Creates an input string stream (iss) from the string variable 'request'.
    //    - This allows us to use stream extraction operators (>>) to parse the string
    //      in a convenient, token-by-token manner, similar to how std::cin works.
    //    - The input stream treats whitespace (spaces, tabs, newlines) as delimiters.

    std::istringstream iss(request);
    //    - Declares three string variables to hold the components of the request line:
    //        method  → HTTP method (e.g., "GET", "POST", "PUT")
    //        path    → Requested resource path (e.g., "/index.html")
    //        version → HTTP version (e.g., "HTTP/1.1")
    std::string method, path, version;
    //    - Extracts tokens sequentially from the request string stream and assigns them
    //      to 'method', 'path', and 'version' respectively.
    //    - After this operation:
    //         method  contains the HTTP method,
    //         path    contains the requested URL/path,
    //         version contains the HTTP protocol version.
    //    - Any extra parts of the request (e.g., headers or body) remain unprocessed
    //      in the stream and can be handled later if needed.
    iss >> method >> path >> version;

    std::string response;

    // Extract the query string (if any) from the URL path in an HTTP request.
    //
    // Example:
    //     If path = "/search?query=ai&sort=desc"
    //     Then after this block executes:
    //         path  = "/search"
    //         query = "query=ai&sort=desc"

    std::string query;

    //    - Searches for the position of the first '?' character in 'path'.
    //    - If found, 'query_pos' will hold its index within the string.
    //    - If not found, std::string::npos is returned (a special value meaning “not found”).
    size_t query_pos = path.find('?');

    //    - Checks whether a '?' was actually found in the path.
    //    - Only if it exists do we proceed to extract the query string.
    if (query_pos != std::string::npos)
    {
        //    - Extracts the substring starting just after '?' till the end of 'path'.
        //    - This gives the query string (e.g., "query=ai&sort=desc").
        query = path.substr(query_pos + 1);
        //    - Truncates the 'path' string to exclude the query part.
        //    - Keeps only the portion before '?' (e.g., "/search").
        path = path.substr(0, query_pos);
    }

    // Extract the HTTP request body (if any) from the complete request string.
    //
    // In an HTTP request, headers and the body are separated by a blank line,
    // represented by the sequence "\r\n\r\n".
    //
    // Example of a raw HTTP request:
    //     POST /submit HTTP/1.1\r\n
    //     Host: example.com\r\n
    //     Content-Length: 13\r\n
    //     \r\n
    //     name=Manish
    //
    // After this block executes on the above example:
    //     body = "name=Manish"

    std::string body;

    //    - Searches for the position of the sequence "\r\n\r\n" in the request string.
    //    - This sequence marks the end of the HTTP headers and the start of the body.
    //    - If found, 'body_pos' will hold the index of the *first* '\r' in that sequence.
    //    - If not found, std::string::npos is returned, meaning the request likely has no body.
    size_t body_pos = request.find("\r\n\r\n");

    //    - Checks whether the header-body separator was found.
    //    - If yes, proceed to extract the body.
    if (body_pos != std::string::npos)
    {
        //    - Extracts the substring starting 4 characters after "\r\n\r\n" (to skip the separator).
        //    - The remainder of the string is the request body.
        body = request.substr(body_pos + 4);
    }

    // Handle different HTTP API endpoints and methods based on the parsed 'path' and 'method'.
    //
    // This block routes incoming HTTP requests to the appropriate handler functions
    // depending on the request URL (path) and HTTP method.
    //
    // Supported endpoints:
    //   1. /api/kv   → Key-Value store operations (CRUD)
    //   2. /stats    → Server statistics
    //   Otherwise → 404 Not Found
    if (path == "/api/kv")
    {

        if (method == "POST")
        {
            // Calls handlePutRequest(body)
            // Used for creating or updating a key-value pair.
            // Expects data in the request body, e.g., {"key": "name", "value": "Manish"}.
            response = handlePutRequest(body, database); // Create/Update
        }

        else if (method == "GET")
        {
            // Calls handleGetRequest(query)
            //  Retrieves the value associated with a given key.
            //  Expects a query string in the URL, e.g., /api/kv?key=name.
            response = handleGetRequest(query, database); // Read
        }

        else if (method == "DELETE")
        {
            // Calls handleDeleteRequest(query)
            // Deletes a key-value pair identified by the key in the query string.
            response = handleDeleteRequest(query, database); // Delete
        }
        else
        {
            // If any other HTTP method is used (e.g., PUT, PATCH, OPTIONS),
            // respond with HTTP 405 "Method Not Allowed".
            // The response body is a JSON error message: {"error": "Method not allowed"}.
            response = buildHttpResponse(405, "{\"error\":\"Method not allowed\"}");
        }
    }
    // Handles a special endpoint that reports server performance statistics.
    // Useful for monitoring purposes.
    else if (path == "/stats")
    {

        std::ostringstream stats; // Create a std::ostringstream object 'stats' to build a JSON response dynamically.
        stats << "{\"total_requests\":" << total_requests
              << ",\"cache_hits\":" << cache_hits
              << ",\"cache_misses\":" << cache_misses
              << ",\"hit_rate\":" << (total_requests > 0 ? (double)cache_hits / total_requests : 0)
              << "}";
        // The constructed JSON string might look like:
        //           {"total_requests":120,"cache_hits":85,"cache_misses":35,"hit_rate":0.7083}

        // Send back a 200 OK response with the JSON statistics.
        response = buildHttpResponse(200, stats.str());
    }
    // Invalid endpoint
    else
    {

        //    - This branch handles invalid or unknown endpoints (not matching /api/kv or /stats).
        //    - Responds with HTTP 404 "Not Found" and a JSON error message:
        response = buildHttpResponse(404, "{\"error\":\"Not found\"}");
    }

    // Send back the HTTP response
    // 0 means default behavior (blocking send)
    send(client_socket, response.c_str(), response.length(), 0);
}

// =======================
// Handle POST/PUT Request
// =======================
std::string KVServer::handlePutRequest(const std::string &body, Database* database)
{
    std::string key, value;
    parseKeyValue(body, key, value); // Extract key and value from JSON

    if (key.empty())
    {
        return buildHttpResponse(400, "{\"error\":\"Invalid request body\"}");
    }

    // Write key-value pair to database
    // If database write fails, return 500 error
    if (!database->put(key, value))
    {
        std::cerr << "[ERROR] PUT failed for key: " << key << std::endl;
        return buildHttpResponse(500, "{\"error\":\"Database write failed\"}");
    }

    // Update in-memory cache as well

    cache->put(key, value);

    return buildHttpResponse(200, "{\"status\":\"success\"}");
}

// =======================
// Handle GET Request
// =======================
std::string KVServer::handleGetRequest(const std::string &query, Database *database)
{
    // It searches the query string for a parameter named "key=".
    // Returns the substring after "key=" up to the next '&' (if any) or the end.
    // e.g., for "key=user123&value=abc", it would return "user123".
    // If "key=" is not found, it should return an empty string.
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
std::string KVServer::handleDeleteRequest(const std::string &query, Database *database)
{
    std::string key = parseKeyFromQuery(query);

    if (key.empty())
    {
        return buildHttpResponse(400, "{\"error\":\"Missing key parameter\"}");
    }

    // Remove from database and cache
    if (!database->del(key)) {
        std::cerr << "[ERROR] DELETE failed for key: " << key << std::endl;
        return buildHttpResponse(500, "{\"error\":\"Database delete failed\"}");
    }
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
    // Find the end of the key parameter (next '&' or end of string)
    size_t end = query.find('&', start);

    if (end == std::string::npos)
    {
        // If no '&' found, return substring from start to end of string
        return query.substr(start);
    }
    // Return substring from start to the position of '&'
    return query.substr(start, end - start);
}

// =======================
// Parse key-value from JSON body
// =======================
void KVServer::parseKeyValue(const std::string &body, std::string &key, std::string &value)
{
    // Simplified JSON parsing for {"key":"...", "value":"..."}
    // Find positions of "key" and "value" in the body string
    size_t key_start = body.find("\"key\"");
    size_t value_start = body.find("\"value\"");

    // If either "key" or "value" is not found, return without modifying key/value
    if (key_start == std::string::npos || value_start == std::string::npos)
    {
        return;
    }

    // Extract the value of the "key" field from a JSON-formatted HTTP request body.
    //
    // Example input body (from a POST request):
    //     {"key":"username","value":"Manish"}
    //
    // After this block executes:
    //     key = "username"

    //    - Finds the position of the colon ':' that follows the "key" field name.
    //    - 'key_start' is assumed to be the position where the substring "key" was found earlier.
    //    - Adding +1 moves the index to just after the colon.
    //    - Example: For '{"key":"username"}', this points right before the first '"' in the value.
    size_t key_value_start = body.find(':', key_start) + 1;

    //    - Finds the first double quote '"' after the colon (the start of the actual value).
    //    - Adds +1 to move past the opening quote.
    //    - After this line, key_value_start points to the beginning of the value text
    //      (e.g., the 'u' in "username").
    key_value_start = body.find('"', key_value_start) + 1;

    //    - Finds the next double quote '"' that marks the *end* of the key's value.
    //    - For example, in {"key":"username"}, this finds the quote after "username".
    size_t key_value_end = body.find('"', key_value_start);

    //    - Extracts the substring between the starting and ending quotes.
    //    - Effectively retrieves just the value part — e.g., "username".
    //    - Stores it into the variable 'key'.
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
        // Ensure the thread is joinable before calling join(), means it is still running
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
