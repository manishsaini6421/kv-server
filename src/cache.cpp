#include "cache.hpp"     // Include the corresponding header file for LRUCache class definition
#include <unordered_map> // For O(1) average time complexity key lookup
#include <list>          // For O(1) time complexity item insertion/deletion at both ends, and efficient splicing
#include <mutex>         // For thread safety

// --- PIMPL Implementation Struct Definition ---

// Defines the hidden implementation details of the LRUCache.
// This struct will hold all the necessary data structures.

struct LRUCache::Impl
{
    // Stores the maximum number of key-value pairs the cache can hold.
    size_t capacity;

    // A Doubly Linked List to maintain the usage order.
    // The front of the list (begin()) is the Most Recently Used (MRU) item.
    // The back of the list (end()) is the Least Recently Used (LRU) item.
    // Each node stores a pair of <key, {value,version no.}>.

    std::list<std::pair<std::string, std::string>> item_list;

    // A Hash Map (unordered_map) to provide O(1) average time complexity lookup by key.
    // The value associated with the key is an iterator pointing to the item's location
    // in the 'item_list' linked list. This allows for O(1) updates to the list order.
    std::unordered_map<std::string, decltype(item_list.begin())> item_map;

    // A Mutex to protect the shared data ('item_list' and 'item_map') from simultaneous
    // access by multiple threads, ensuring the cache is thread-safe.
    std::mutex mtx;

    // Constructor for the implementation struct.
    Impl(size_t cap) : capacity(cap) {}
};

// --- LRUCache Public Methods Implementation ---

/**
 * @brief Constructor for LRUCache. Allocates the implementation struct.
 * @param capacity The maximum size of the cache.
 */
LRUCache::LRUCache(size_t capacity)
{
    // Create and initialize the private implementation pointer.
    cache_impl = new Impl(capacity);
}

/**
 * @brief Destructor for LRUCache. Cleans up the allocated implementation struct.
 */
LRUCache::~LRUCache()
{
    // Deallocate the private implementation object to prevent memory leaks.
    delete cache_impl;
}

/**
 * @brief Retrieves the value associated with a key, and marks the item as MRU.
 * @param key The key to look up.
 * @return The value if found, or an empty string if not found.
 */
std::string LRUCache::get(const std::string &key)
{
    // Lock the mutex: ensures exclusive access to the cache data for this operation.
    std::lock_guard<std::mutex> lock(cache_impl->mtx);

    // 1. Check if the key exists in the map.
    auto it = cache_impl->item_map.find(key);
    // If the key is not found, return an empty string immediately.
    if (it == cache_impl->item_map.end())
        return {"", 0};

    // 2. The item was found: it's now the Most Recently Used (MRU).
    // Use std::list::splice to move the list node pointed to by 'it->second'
    // from its current position to the front of the list (cache_impl->item_list.begin()).
    // This is an O(1) operation as it only rearranges pointers.
    cache_impl->item_list.splice(cache_impl->item_list.begin(), cache_impl->item_list, it->second);

    // 3. The value is the 'second' element of the list node (it->second is the list iterator,
    // which points to a std::pair<key, value>, so ->second is the value part).
    return it->second->second;
}

/**
 * @brief Inserts or updates a key-value pair, managing cache capacity.
 * @param key The key to insert/update.
 * @param value The value to associate with the key.
 */
void LRUCache::put(const std::string &key, const std::string &value)
{
    // Lock the mutex: ensures exclusive access to the cache data.
    std::lock_guard<std::mutex> lock(cache_impl->mtx);

    // 1. Check if the key already exists (Update case).
    auto it = cache_impl->item_map.find(key);
    if (it != cache_impl->item_map.end())
    {
        // Key found: Update the value in the list node.
        
            it->second->second = value;

            // Mark as MRU: Move the node to the front of the list (O(1)).
            cache_impl->item_list.splice(cache_impl->item_list.begin(), cache_impl->item_list, it->second);
        

        return; // Operation complete.
    }

    // 2. Key not found (New insertion case): Check for capacity overflow.
    if (cache_impl->item_list.size() >= cache_impl->capacity)
    {
        // A. Capacity exceeded: Find the Least Recently Used (LRU) item.
        // The LRU item is always the last element in the list.
        auto last = cache_impl->item_list.back();

        // B. Remove the LRU item from the map.
        cache_impl->item_map.erase(last.first);

        // C. Remove the LRU item from the list.
        cache_impl->item_list.pop_back();
    }

    // 3. Insert the new item.
    // A. Add the new pair to the front of the list (MRU position).
    cache_impl->item_list.emplace_front(key, value);

    // B. Store the key and the iterator to the new list node in the map.
    // cache_impl->item_list.begin() now points to the newly inserted node.
    cache_impl->item_map[key] = cache_impl->item_list.begin();
}

/**
 * @brief Removes a key-value pair from the cache.
 * @param key The key to delete.
 */
void LRUCache::del(const std::string &key)
{
    // Lock the mutex: ensures exclusive access to the cache data.
    std::lock_guard<std::mutex> lock(cache_impl->mtx);

    // 1. Find the key in the map.
    auto it = cache_impl->item_map.find(key);
    // If not found, nothing to do, return.
    if (it == cache_impl->item_map.end())
        return;

    // 2. Remove the item from the list using the stored iterator (it->second).
    cache_impl->item_list.erase(it->second);

    // 3. Remove the entry from the map.
    cache_impl->item_map.erase(it);
}