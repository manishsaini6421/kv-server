# I’m setting the minimum required CMake version to 3.10
# This ensures the project works on systems with CMake 3.10 or above
cmake_minimum_required(VERSION 3.10)

# Naming my project as KVServer
# This name will be used for the build target and project identification
project(KVServer)

# I want to use the C++17 standard for this project
set(CMAKE_CXX_STANDARD 17)

# Making sure the compiler strictly uses C++17 (no fallback to older standards)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Adding compiler flags:
# -pthread → enables multithreading support
# -O3 → enables high-level compiler optimization
# -Wall → enables almost all compiler warnings (helps in debugging)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall")

# I need PostgreSQL for database connectivity in my server
# CMake will automatically find and include PostgreSQL libraries and headers
find_package(PostgreSQL REQUIRED)

# I’m using Boost for system-level utilities (like networking or error handling)
# Specifically, I’m including the ‘system’ component
find_package(Boost REQUIRED COMPONENTS system)

# Adding include directories so the compiler can find header files
# Includes PostgreSQL, Boost, and my project’s src folder
include_directories(
    ${PostgreSQL_INCLUDE_DIRS}   # PostgreSQL headers
    ${Boost_INCLUDE_DIRS}        # Boost headers
    ${CMAKE_SOURCE_DIR}/src      # My source folder
)

# ==============================================
# Building the main Key-Value Server executable
# ==============================================

# Creating the server executable named "kv_server"
# It will compile all the following source files together:
# main.cpp → entry point
# server.cpp → handles HTTP connections and requests
# database.cpp → manages PostgreSQL database operations
# cache.cpp → implements in-memory cache
add_executable(kv_server
    src/main.cpp
    src/server.cpp
    src/database.cpp
    src/cache.cpp
)

# Linking all required libraries with my kv_server executable:
# - PostgreSQL → for database access
# - Boost → for system utilities
# - pthread → for multi-threading support
target_link_libraries(kv_server
    ${PostgreSQL_LIBRARIES}
    ${Boost_LIBRARIES}
    pthread
)

# ==============================================
# Building the Load Generator executable
# ==============================================

# Creating another executable called "load_generator"
# This program generates workloads (PUT, GET, MIXED etc.)
# to test how well the server handles concurrent requests
add_executable(load_generator
    src/load_generator.cpp
)

# Linking libraries for the load generator:
# - Boost → used for utilities or system functions
# - pthread → for running multiple threads to generate load
target_link_libraries(load_generator
    ${Boost_LIBRARIES}
    pthread
)
