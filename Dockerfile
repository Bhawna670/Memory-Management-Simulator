
FROM ubuntu:24.04

# Install build tools and Git for CMake FetchContent.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the complete project.
COPY . .

# Configure and build the API.
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target memsim_api --parallel 2

# Render provides the PORT environment variable.
EXPOSE 8080

# Start the API and static frontend server.
CMD ["./build/memsim_api"]