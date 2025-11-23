# HTTP-based Key-Value Server with LRU Cache
A high-performance, multi-threaded HTTP server implementing a key-value store with LRU caching and PostgreSQL persistence. Built in C++17 with Docker containerization for easy deployment and testing.

---

## 📋 Table of Contents

- [Overview](#overview)
- [System Architecture](#system-architecture)
- [Features](#features)
- [Prerequisites](#prerequisites)
- [Quick Start](#quick-start)
- [API Documentation](#api-documentation)
- [Request Execution Paths](#request-execution-paths)
- [Load Testing](#load-testing)
- [Configuration](#configuration)
- [Development](#development)
- [Project Structure](#project-structure)
- [Performance Analysis](#performance-analysis)
- [Troubleshooting](#troubleshooting)
- [Authors](#authors)

---

## 🎯 Overview

This project implements a **multi-tier HTTP-based Key-Value store** designed to demonstrate different performance bottlenecks in distributed systems. The system serves as both a functional KV store and a platform for performance analysis and load testing.

### Key Highlights

- Multi-threaded HTTP server with configurable thread pool
- LRU Cache for low-latency memory access
- PostgreSQL Database for persistent storage
- RESTful API supporting GET, POST, DELETE operations
- Docker Compose orchestration for easy deployment
- Load Generator with multiple workload types
- Performance Metrics tracking and statistics

---

## 🏗️ System Architecture
```
                                ┌─────────────────────────────┐
                                │    Client (HTTP Requests)   │           
                                └───────────┬─────────────────┘
                                            │
                                    HTTP GET/POST/DELETE
                                            │  
                                            ▼
                                ┌─────────────────────────────┐
                                │     HTTP Server (C++)       │
                                │ (Thread pool & LRU Cache)   │
                                └───────────┬─────────────────┘
                                            │
                                    SQL queries (libpq)
                                            │
                                            ▼
                                ┌─────────────────────────────┐
                                │   PostgreSQL Database       │
                                │        (Docker)             │
                                └─────────────────────────────┘
```

### Architecture Components

1. **HTTP Server Layer**  
   - Multi-threaded request handling with worker thread pool  
   - RESTful API implementation  
   - Request routing and parsing  
   - Statistics tracking (cache hits/misses, throughput)

2. **Cache Layer (LRU)**  
   - In-memory hash map + doubly linked list  
   - O(1) time complexity for all operations  
   - Thread-safe with mutex protection  
   - PIMPL idiom for encapsulation

3. **Database Layer (PostgreSQL)**  
   - Persistent storage for all key-value pairs  
   - Connection pooling and reconnection logic  
   - Indexed table for fast lookups  
   - Automatic timestamp tracking

---
```
                         ┌────────────────────────────────────┐
                         │      KV Server (1 Container)       │
                         │  ┌──────────────────────────────┐  │
                         │  │ Thread 1 → DB Connection 1   │  │
                         │  │ Thread 2 → DB Connection 2   │  │
                         │  │ Thread 3 → DB Connection 3   │  │
                         │  │ ...                          │  │
                         │  │ Thread n → DB Connection n   │  │
                         │  │ (All connecting to SAME DB)  │  │
                         │  └──────────────────────────────┘  │
                         └───────┬──┬──┬──┬──┬──┬──┬──┬───────┘
                                 │  │  │  │  │  │  │  │
                               (Multiple TCP Connections)
                                 │  │  │  │  │  │  │  │
                                 ▼  ▼  ▼  ▼  ▼  ▼  ▼  ▼
                       ┌─────────────────────────────────────────┐
                       │   Single PostgreSQL Database Container  │
                       │        (kv_postgres running)            │
                       └─────────────────────────────────────────┘
                             
```

## ✨ Features

### Core Functionality

- RESTful API with JSON responses  
- Thread-safe LRU cache with configurable capacity  
- Persistent storage in PostgreSQL  
- Automatic cache population on database reads  
- Statistics endpoint for monitoring cache performance  
- Docker Compose for one-command deployment

### Request Types

1. **CREATE/UPDATE** (`POST /api/kv`)  
   - Stores in both cache and database  
   - Atomic operation ensuring consistency

2. **READ** (`GET /api/kv?key=<key>`)  
   - **Path 1 (Cache Hit):** Returns from memory (low latency)  
   - **Path 2 (Cache Miss):** Fetches from database, populates cache

3. **DELETE** (`DELETE /api/kv?key=<key>`)  
   - Removes from both cache and database

### Load Testing

- **PUT_ALL:** Write-heavy workload (disk-bound)  
- **GET_ALL:** Read workload with cache misses (disk-bound)  
- **GET_POPULAR:** Read workload with cache hits (CPU-bound)  
- **MIXED:** Balanced workload with random operations

---

## 📦 Prerequisites

- **Docker** (version 20.10+)  
- **Docker Compose** (version 2.0+)  
- **Git** (for cloning the repository)  
- Optional (local development): C++17 compiler, CMake, libpq-dev, Boost

---

## 🚀 Quick Start

### Repository

git clone https://github.com/manishsaini6421@gmail.com/kv-server.git
cd kv-server


### Build and Run
```
# Step 1: Stop and remove all containers
    docker-compose down

# Step 2: Remove the problematic container
    docker rm -f kv_server kv_postgres

# Step 3: Clean up dangling volumes
    docker volume prune -f

# Step 4: Remove cached images (optional but recommended)
    docker rmi kv-server_kv_server

# Step 5: Start fresh
    docker-compose up --build



Expected output:

kv_postgres | database system is ready to accept connections
kv_server | Successfully connected to PostgreSQL database
kv_server | KV Server listening on port 8080 
kv_server | Server is running. Press Ctrl+C to stop.

```

### Test

curl http://localhost:8080/stats

curl -X POST http://localhost:8080/api/kv -H "Content-Type: application/json" -d '{"key":"hello","value":"world"}'

curl http://localhost:8080/api/kv?key=hello

curl -X DELETE http://localhost:8080/api/kv?key=hello


---

## 📡 API Documentation

- `POST /api/kv`: Create/Update key-value pair
- `GET /api/kv?key=<key>`: Read key
- `DELETE /api/kv?key=<key>`: Delete key
- `GET /stats`: Cache and request statistics

---

## 🔄 Request Execution Paths

- **Cache Hit:** Returns from in-memory LRU cache with low latency
- **Cache Miss:** Reads from PostgreSQL DB and caches the result
- **Write Requests:** Synchronously update DB and cache

---

## 🧪 Load Testing

Run in container:

*docker-compose exec kv_server /bin/bash*

*./load_generator <host> <port> <workload> <num_threads> <duration_in_sec> [key_space_size]*

# Different workloads
```
./build/load_generator localhost 8080 PUT_ALL 20 300      # Disk-bound
./build/load_generator localhost 8080 GET_POPULAR 10 300  # CPU-bound
./build/load_generator localhost 8080 MIXED 15 300        # Mixed

```

---



## Authors

Manish Kumar Saini



