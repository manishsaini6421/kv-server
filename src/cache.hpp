#pragma once // Ensures this header file is included only once during compilation.
#include <string> // Includes the standard string class, used for keys and values.

/**
 * @brief Represents a thread-safe, fixed-size Least Recently Used (LRU) cache.
 * * An LRU cache stores key-value pairs and, when full, removes the item
 * that hasn't been accessed for the longest time to make space for new items.
 */

class LRUCache
{
private:
    /* data */
    // --- PIMPL Idiom Start ---
    
    // Forward declaration of the private implementation structure.
    // This hides all internal data members (like the map and list)
    // from users of the header file, reducing compilation dependencies.
    struct Impl;
    
    // Pointer to the actual implementation structure.
    // This is the "pointer to implementation" part of the PIMPL idiom.
    Impl* cache_impl;

    // --- PIMPL Idiom End ---

    

public:
    /**
     * @brief Constructor for the LRUCache.
     * @param capacity The maximum number of items the cache can hold.
     */
    LRUCache(size_t capacity);
    
    /**
     * @brief Destructor for the LRUCache.
     * * It is responsible for cleaning up the memory allocated for the 'impl' object.
     */
    ~LRUCache();
    
    /**
     * @brief Retrieves the value associated with the given key.
     * * If found, the item is moved to the Most Recently Used (MRU) position.
     * * @param key The key to look up.
     * @return The value associated with the key, or an empty string if the key is not found.
     */
    std::string get(const std::string& key);
    
    /**
     * @brief Inserts or updates a key-value pair in the cache.
     * * If the key already exists, its value is updated and it becomes MRU.
     * If the key is new and the cache is full, the Least Recently Used (LRU) item is evicted.
     * * @param key The unique key for the item.
     * @param value The data to be stored.
     */
    void put(const std::string& key, const std::string& value);
    
    /**
     * @brief Explicitly removes a key-value pair from the cache.
     * * @param key The key of the item to delete.
     */
    void del(const std::string& key);
    
};