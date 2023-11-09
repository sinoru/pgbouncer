FROM alpine AS base

RUN \
    --mount=type=cache,target=/var/cache,sharing=locked \
    --mount=type=tmpfs,target=/var/log \
    set -eux; \
    apk add --no-cache \
        c-ares \
        libevent \
        openssl \
        tini \
    ;

RUN set -eux; \
    adduser -SHD pgbouncer;

################################################################################

FROM base AS builder

RUN \
    --mount=type=cache,target=/var/cache,sharing=locked \
    --mount=type=tmpfs,target=/var/log \
    set -eux; \
    apk add --no-cache \
        autoconf \
        automake \
        build-base \
        c-ares-dev \
        git \
        libevent-dev \
        libtool \
        m4 \
        openssl-dev \
        pandoc-cli \
        pkgconfig \
    ;

WORKDIR /opt/pgbouncer
COPY --link . /opt/pgbouncer

RUN set -eux; \
    git submodule update --init; \
    ./autogen.sh; \
    ./configure \
        --prefix=/usr/local \
        --with-cares \
    ; \
    make; \
    make install;

################################################################################

FROM base

COPY --link --from=builder /usr/local /usr/local

# Smoke test
RUN /usr/local/bin/pgbouncer --help

USER pgbouncer
ENTRYPOINT ["/sbin/tini", "--"]
STOPSIGNAL SIGINT
EXPOSE 6432

CMD ["/usr/local/bin/pgbouncer"]
