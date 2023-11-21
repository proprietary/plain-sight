FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libopencv-dev \
    libavcodec-dev \
    libavformat-dev \
    libswscale-dev \
    libavutil-dev \
    libavresample-dev \
    libavfilter-dev

COPY .  /usr/src/app

WORKDIR /var/app

RUN cmake /usr/src/app && make

CMD ["./app"]