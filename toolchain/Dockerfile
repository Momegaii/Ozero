# Stage 0 build environment. Pin DEBIAN_IMAGE by digest for a release build:
#   docker build --build-arg DEBIAN_IMAGE=debian:trixie-slim@sha256:<digest> ...
ARG DEBIAN_IMAGE=debian:trixie-slim
FROM ${DEBIAN_IMAGE}

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      binutils \
      make \
      qemu-system-x86 \
      git \
      ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /work
CMD ["make", "ci"]
