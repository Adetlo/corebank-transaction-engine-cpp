# syntax=docker/dockerfile:1.7
# Multi-stage build: compilers stay in the builder, the runtime image ships only the
# binaries, libsqlite3 and libstdc++, and runs as an unprivileged user.

FROM debian:bookworm-slim AS builder
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build libsqlite3-dev ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY cmake ./cmake
COPY include ./include
COPY src ./src
COPY config ./config
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCOREBANK_BUILD_TESTS=OFF -DCOREBANK_BUILD_BENCH=ON \
 && cmake --build build --parallel \
 && strip build/corebank build/corebank-bench

FROM debian:bookworm-slim AS runtime
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends libsqlite3-0 bash \
 && rm -rf /var/lib/apt/lists/* \
 && groupadd --system --gid 10001 corebank \
 && useradd --system --uid 10001 --gid corebank --home-dir /srv/corebank --shell /usr/sbin/nologin corebank \
 && install -d -o corebank -g corebank -m 0750 /srv/corebank /srv/corebank/var /srv/corebank/var/batch
COPY --from=builder /src/build/corebank /src/build/corebank-bench /usr/local/bin/
COPY --chown=corebank:corebank config/default.yaml /etc/corebank/default.yaml
COPY --chown=corebank:corebank scripts /srv/corebank/scripts

ENV COREBANK_CONFIG=/etc/corebank/default.yaml \
    COREBANK_DB=/srv/corebank/var/corebank.db \
    COREBANK_BATCH_DIR=/srv/corebank/var/batch \
    COREBANK_LISTEN_ADDRESS=0.0.0.0 \
    COREBANK_PORT=7400 \
    COREBANK_LOG_LEVEL=INFO \
    COREBANK_HOME=/srv/corebank \
    COREBANK_BIN=/usr/local/bin/corebank
WORKDIR /srv/corebank
USER corebank:corebank
EXPOSE 7400
VOLUME ["/srv/corebank/var"]
HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=3 \
  CMD ["corebank", "health", "--host", "127.0.0.1", "--port", "7400"]
# SIGTERM triggers the engine's graceful shutdown (drain queue, audit ENGINE_STOPPED).
STOPSIGNAL SIGTERM
ENTRYPOINT ["corebank"]
CMD ["serve"]
