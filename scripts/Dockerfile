FROM debian:bookworm

RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
