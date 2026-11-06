# ==============================================================================
# IMPULSE GRAPH ENGINE TOOLKIT CONTAINER (ghcr.io/impulse-graph/impulse-tools)
# Multi-stage ultra-lean static build for CI/CD agents & Kubernetes batch jobs
# ==============================================================================

# ------------------------------------------------------------------------------
# Stage 1: Build static binaries using Rust musl target
# ------------------------------------------------------------------------------
FROM rust:1.80-alpine AS builder

RUN apk add --no-cache musl-dev gcc git

WORKDIR /workspace/impulse-graph-core
COPY . .

WORKDIR /workspace/impulse-graph-core/impulse-cli

# Build release binary
RUN cargo build --release

# ------------------------------------------------------------------------------
# Stage 2: Ultra-lean runtime container (<15 MB)
# ------------------------------------------------------------------------------
FROM alpine:3.20 AS runtime

RUN apk add --no-cache bash ca-certificates

COPY --from=builder /workspace/impulse-graph-core/impulse-cli/target/release/impulse-graph /usr/local/bin/impulse-graph
RUN ln -s /usr/local/bin/impulse-graph /usr/local/bin/impulse

ENTRYPOINT ["/usr/local/bin/impulse-graph"]
CMD ["--help"]
