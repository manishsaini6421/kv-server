#include "database.hpp"  // Includes the header file where the Database class and its member functions are declared.
#include <iostream>      // For input-output operations (std::cout, std::cerr).
#include <sstream>       // For building strings efficiently using std::ostringstream.
#include <cstring>       // For C-style string operations if needed (not directly used here but good for compatibility).

// ==========================================================================================
// Constructor: Establishes a connection to the PostgreSQL database using given parameters.
// ==========================================================================================
Database::Database(const std::string& host, const std::string& port,
                   const std::string& dbname, const std::string& user,
                   const std::string& password) {
     // Build connection string dynamically using an output string stream (oss).
     // Each connection parameter (host, port, dbname, user, password) is concatenated properly.

    std::ostringstream oss;  // Create a string stream to combine all parameters into a single string.

    // Build the connection string in the PostgreSQL expected format:
    // "host=<host> port=<port> dbname=<dbname> user=<user> password=<password>"
    oss << "host=" << host << " port=" << port << " dbname=" << dbname
        << " user=" << user << " password=" << password;

    // Store the connection string as a member variable for later use (e.g., reconnection).
    connection_string = oss.str();

    // Attempt to connect to the PostgreSQL database using PQconnectdb.
    // PQconnectdb() is a libpq function that establishes a new database connection using the given string.
    conn = PQconnectdb(connection_string.c_str());

    // Check if the connection attempt was successful.
    // PQstatus(conn) returns the current status of the connection.
    if (PQstatus(conn) != CONNECTION_OK) {
        // If connection failed, print the error message to standard error.
        std::cerr << "Connection to database failed: " << PQerrorMessage(conn) << std::endl;
        
        // Clean up the failed connection by calling PQfinish, which closes and frees the PGconn object.
        PQfinish(conn);
        conn = nullptr;  // Set connection pointer to null for safety.
    } else {
        // If the connection is successful, print a success message.
        std::cout << "Successfully connected to PostgreSQL database" << std::endl;
    }
}

// ==========================================================================================
// Destructor: Cleans up the database connection when the Database object goes out of scope.
// ==========================================================================================
Database::~Database() {
    if (conn) {        // If the connection is still open (not null)...
        PQfinish(conn); // ...close the connection and release associated resources.
    }
}

// ==========================================================================================
// Attempt to reconnect to the database using the stored connection string.
// ==========================================================================================
bool Database::reconnect() {
    if (conn) {
        // Close existing connection first to avoid leaks or dangling pointers.
        PQfinish(conn);
    }
    // Re-establish a new connection using the same connection string.
    conn = PQconnectdb(connection_string.c_str());

    // Return true if the new connection status is OK, otherwise false.
    return PQstatus(conn) == CONNECTION_OK;
}

// ==========================================================================================
// Check if the connection is alive; if not, attempt to reconnect.
// ==========================================================================================
void Database::checkConnection() {
    // If connection pointer is null or its status is not OK, reconnect.
    if (!conn || PQstatus(conn) != CONNECTION_OK) {
        std::cerr << "Database connection lost. Attempting to reconnect..." << std::endl;
        reconnect();
    }
}

// ==========================================================================================
// Escape a string so that it can be safely used in SQL queries (prevents SQL injection).
// ==========================================================================================
std::string Database::escapeString(const std::string& str) {
    // Allocate enough space for the escaped version of the string.
    // Each character may become up to 2 characters in the escaped version, hence length*2 + 1.
    char* escaped = new char[str.length() * 2 + 1];

    // Use PostgreSQL's built-in escape function to sanitize the string.
    // PQescapeStringConn() escapes special characters in 'str' for safe SQL usage.
    // The last parameter is for error reporting, which we set to nullptr here.
    PQescapeStringConn(conn, escaped, str.c_str(), str.length(), nullptr);

    // Convert the escaped C-string to a std::string for ease of use in C++.
    std::string result(escaped);

    // Free the dynamically allocated memory.
    delete[] escaped;

    // Return the escaped string.
    return result;
}

// ==========================================================================================
// PUT operation: Create or update a key-value pair in the database (Upsert).
// ==========================================================================================
bool Database::put(const std::string& key, const std::string& value) {
    checkConnection();   // Ensure the connection is valid before executing SQL.
    if (!conn) return false;  // If connection is invalid, return false immediately.

    // Escape the key and value to avoid SQL injection.
    std::string escaped_key = escapeString(key);
    std::string escaped_value = escapeString(value);

    // Build an SQL query for "INSERT ... ON CONFLICT (key) DO UPDATE".
    // This ensures if the key already exists, it updates its value instead of inserting a duplicate.
    std::ostringstream query;
    query << "INSERT INTO kv_store (key, value) VALUES ('"
          << escaped_key << "', '" << escaped_value << "') "
          << "ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value";

    // Execute the SQL command using PQexec().
    // PQexec() sends a command to the PostgreSQL server and waits for the result.
    PGresult* res = PQexec(conn, query.str().c_str());

    // Check the execution result status.
    ExecStatusType status = PQresultStatus(res);

    // Determine success based on whether the command completed successfully.
    bool success = (status == PGRES_COMMAND_OK);

    // If it failed, print an error message for debugging.
    if (!success) {
        std::cerr << "PUT failed: " << PQerrorMessage(conn) << std::endl;
    }

    // Free the PGresult object to avoid memory leaks.
    PQclear(res);

    // Return whether the operation was successful.
    return success;
}

// ==========================================================================================
// GET operation: Retrieve the value for a given key from the database.
// ==========================================================================================
bool Database::get(const std::string& key, std::string& value) {
    checkConnection();   // Ensure connection is alive before executing.
    if (!conn) return false; // Return false if connection is invalid.

    // Escape the key to ensure safe query execution.
    std::string escaped_key = escapeString(key);

    // Construct a SELECT SQL query to retrieve the value corresponding to the given key.
    std::ostringstream query;
    query << "SELECT value FROM kv_store WHERE key = '" << escaped_key << "'";

    // Execute the SQL query and store the result.
    PGresult* res = PQexec(conn, query.str().c_str());

    // Get the status of the executed query.
    ExecStatusType status = PQresultStatus(res);

    bool found = false; // Boolean flag to indicate whether the key was found.

    // If the query succeeded and returned at least one row...
    if (status == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        // Extract the value from the first row and first column.
        value = PQgetvalue(res, 0, 0);
        found = true; // Mark as found.
    }

    // Clean up the PGresult object.
    PQclear(res);

    // Return whether the key was found in the table.
    return found;
}

// ==========================================================================================
// DELETE operation: Remove a key-value pair from the database.
// ==========================================================================================
bool Database::del(const std::string& key) {
    checkConnection();  // Ensure connection is valid.
    if (!conn) return false;  // If not connected, return false.

    // Escape the key to prevent SQL injection.
    std::string escaped_key = escapeString(key);

    // Build the SQL query to delete the record matching the given key.
    std::ostringstream query;
    query << "DELETE FROM kv_store WHERE key = '" << escaped_key << "'";

    // Execute the DELETE command.
    PGresult* res = PQexec(conn, query.str().c_str());

    // Get the result status.
    ExecStatusType status = PQresultStatus(res);

    // If command executed successfully, mark as success.
    bool success = (status == PGRES_COMMAND_OK);

    // If deletion failed, print the error for debugging.
    if (!success) {
        std::cerr << "DELETE failed: " << PQerrorMessage(conn) << std::endl;
    }

    // Clear the result to release memory.
    PQclear(res);

    // Return true if deletion succeeded, else false.
    return success;
}

// ==========================================================================================
// Utility function: Check if the database connection is currently alive.
// ==========================================================================================
bool Database::isConnected() {
    // Returns true only if connection pointer is valid and status is OK.
    return conn && PQstatus(conn) == CONNECTION_OK;
}
