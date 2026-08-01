FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# --- Base system & build tools ---
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    wget \
    curl \
    ca-certificates \
    libpq-dev \
    libpqxx-dev \
    libgoogle-glog-dev \
    libgflags-dev \
    && rm -rf /var/lib/apt/lists/*

# --- Working directory ---
WORKDIR /app

# --- Copy source ---
COPY . .

# --- Build ---
# Adjust path if needed
RUN g++ -std=c++17 crdb/benchmark_crdb.cpp \
    -o benchmark_crdb \
    -lpqxx -lpq -lglog -lgflags -pthread

# Build Detock with CRDB Benchmarks
RUN cmake -B build -DBUILD_CRDB_BENCHMARKS=ON -DBUILD_SLOG_TESTS=OFF -DBUILD_SLOG_CLIENT=OFF
RUN cmake --build build --target benchmark_benchx_crdb -j$(nproc)
# Move it so it can be executed easily
RUN mv build/crdb/benchmark_benchx_crdb . || mv build/benchmark_benchx_crdb .

# --- Default runtime flags ---
ENV GLOG_logtostderr=1

CMD ["./benchmark_crdb"]
