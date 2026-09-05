FROM ubuntu:24.04

LABEL org.opencontainers.image.source="https://github.com/janhubicka/Color-Screen"
LABEL org.opencontainers.image.description="Ubuntu 24.04 build dependencies for Color-Screen GitHub Actions"

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        autoconf \
        autoconf-archive \
        automake \
        build-essential \
        ca-certificates \
        ccache \
        git \
        gzip \
        libexiv2-dev \
        libfftw3-dev \
        libgsl-dev \
        liblcms2-dev \
        libopenjp2-7-dev \
        libpng-dev \
        libraw-dev \
        libtiff-dev \
        libtool \
        libturbojpeg0-dev \
        libzip-dev \
        pkg-config \
        qt6-base-dev \
        qt6-tools-dev \
        qt6-tools-dev-tools \
        qt6-translations-l10n \
        libqt6svg6-dev \
        tar \
        xauth \
        xvfb \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*
