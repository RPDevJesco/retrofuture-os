FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    nasm \
    xorriso \
    mtools \
    && rm -rf /var/lib/apt/lists/*

# Install i686-elf cross compiler
RUN apt-get update && apt-get install -y \
    gcc-multilib \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY . .

RUN mkdir -p output && \
    make clean && \
    make all && \
    make textmode && \
    make programs && \
    cp build/boot.bin output/ && \
    cp build/stage2.bin output/ && \
    cp build/stage2-text.bin output/ && \
    cp build/kernel.bin output/ && \
    cp build/kernel.elf output/ && \
    cp build/RetroFuture.img output/ && \
    cp build/RetroFuture.iso output/ && \
    cp build/RetroFuture-text.img output/ && \
    cp build/RetroFuture-text.iso output/ && \
    cp -r build/programs output/

CMD ["ls", "-la", "output/"]