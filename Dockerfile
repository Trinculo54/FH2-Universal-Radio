FROM ubuntu:24.04 AS builder
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        xz-utils \
        file \
        git \
        cmake \
        ninja-build \
        build-essential \
    && rm -rf /var/lib/apt/lists/*

# install llvm
ARG LLVM_MINGW_VERSION=20260922
RUN curl -fsSL "https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-x86_64.tar.xz" -o /tmp/llvm-mingw.tar.xz \
    && mkdir -p /opt/llvm-mingw \
    && tar -xJf /tmp/llvm-mingw.tar.xz -C /opt/llvm-mingw --strip-components=1 \
    && rm /tmp/llvm-mingw.tar.xz
# Add to PATH
ENV PATH="/opt/llvm-mingw/bin:${PATH}"

# Copy
WORKDIR /src
COPY . .

RUN scripts/get-deps.sh
RUN scripts/build.sh

FROM scratch AS dist
COPY --from=builder /src/dist/ /
