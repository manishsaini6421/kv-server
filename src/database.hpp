#pragma once

#include <string>
#include <memory>
#include <libpq-fe.h>

/**
 * @brief Database connection manager for PostgreSQL
 * 
 * Provides thread-safe database operations for KV store
 */
class Database {
private:
// Pointer to the PostgreSQL connection object
    PGconn* conn;
    // Connection string used to connect/reconnect to the database
    std::string connection_string;
    // Attempt to reconnect to the database
    bool reconnect();
    // Check if the connection is alive, and reconnect if necessary
    void checkConnection();
    // Escape a string to be safely used in SQL queries
    std::string escapeString(const std::string& str);

public:
// Constructor: Establishes a connection to the PostgreSQL database
    Database(const std::string& host, const std::string& port,
             const std::string& dbname, const std::string& user,
             const std::string& password);
    // Destructor: Cleans up the database connection
    ~Database();
    
    /**
     * @brief Create or update a key-value pair in database
     * @param key The key to insert/update
     * @param value The value to store
     * @return true if successful, false otherwise
     */
    bool put(const std::string& key, const std::string& value);
    
    /**
     * @brief Retrieve value for a given key from database
     * @param key The key to lookup
     * @param value Output parameter for the retrieved value
     * @return true if key exists, false otherwise
     */
    bool get(const std::string& key, std::string& value);
    
    /**
     * @brief Delete a key-value pair from database
     * @param key The key to delete
     * @return true if successful, false otherwise
     */
    bool del(const std::string& key);
    
    /**
     * @brief Check if database connection is alive
     * @return true if connected, false otherwise
     */
    bool isConnected();
};
