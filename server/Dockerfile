# syntax=docker/dockerfile:1
FROM golang:1.27-bookworm AS server
WORKDIR /build
COPY go.mod go.sum ./
RUN go mod download
COPY cmd/ cmd/
COPY internal/ internal/
RUN CGO_ENABLED=0 go build -trimpath -ldflags='-s -w' -o /out/picobrew-hub ./cmd/picobrew-hub

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 hub && useradd --uid 10001 --gid hub --no-create-home hub \
    && mkdir /data && chown hub:hub /data
COPY --from=server /out/picobrew-hub /usr/local/bin/picobrew-hub
WORKDIR /app
USER hub
VOLUME ["/data"]
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=3s --start-period=10s \
    CMD curl --fail --silent http://127.0.0.1:8080/healthz > /dev/null || exit 1
ENTRYPOINT ["picobrew-hub"]
CMD ["--listen", "0.0.0.0:8080", "--data", "/data"]
