FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates wget curl unzip xz-utils bzip2 make cmake ninja-build pkg-config python3 git file xxd \
      clang-18 lld-18 llvm-18 libclang-rt-18-dev g++ ccache patch fonts-dejavu-core python3-pil \
      libsdl2-dev libopenal-dev libcurl4-openssl-dev genisoimage p7zip-full \
 && rm -rf /var/lib/apt/lists/*
ENV PATH=/usr/lib/llvm-18/bin:$PATH
WORKDIR /src
