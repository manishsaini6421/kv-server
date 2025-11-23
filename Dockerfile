# Dockerfile for C++ KV Server

FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libpq-dev \
    libboost-all-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source files
COPY src/ ./src/
COPY CMakeLists.txt .

# Build the application
RUN mkdir build && cd build && \
    cmake .. && \
    make -j$(nproc)

# Move binaries to root
RUN mv build/kv_server . && \
    mv build/load_generator .

# Expose port
EXPOSE 8080

# Default command
CMD ["./kv_server"]

