# The legacy floor build environment: ubuntu:22.04 (glibc 2.35) with g++-12.
#   docker build --build-arg BASE="$(cat packaging/linux/floor/image)" \
#       -t hikarisub-floor packaging/linux/floor
ARG BASE
FROM ${BASE}
COPY install-deps.sh /usr/local/sbin/hikarisub-floor-install-deps
RUN /usr/local/sbin/hikarisub-floor-install-deps && rm -rf /var/lib/apt/lists/*
