FROM ubuntu:24.04

# Install build dependencies
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        git \
        make \
        openssh-client \
        python3 \
    && rm -rf /var/lib/apt/lists/*
