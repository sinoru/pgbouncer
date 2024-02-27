FROM alpine AS base
ARG TARGETPLATFORM

RUN \
    --mount=type=cache,id=$TARGETPLATFORM:/var/cache/apk,target=/var/cache/apk,sharing=locked \
    --mount=type=cache,target=$TARGETPLATFORM:/var/cache,sharing=locked \
    --mount=type=tmpfs,target=/var/log \
    set -eux; \
    apk add \
        ca-certificates \
        c-ares \
        libevent \
        libssl3 \
    ;

################################################################################

FROM base AS builder
ARG TARGETPLATFORM

RUN \
    --mount=type=cache,id=$TARGETPLATFORM:/var/cache/apk,target=/var/cache/apk,sharing=locked \
    --mount=type=cache,target=$TARGETPLATFORM:/var/cache,sharing=locked \
    --mount=type=tmpfs,target=/var/log \
    set -eux; \
    apk add \
        autoconf \
        automake \
        build-base \
        c-ares-dev \
        libevent-dev \
        libtool \
        linux-headers \
        openssl-dev \
        pandoc-cli \
        pkgconfig \
    ;

WORKDIR /opt/pgbouncer
COPY --link . /opt/pgbouncer

RUN set -eux; \
    ./autogen.sh; \
    ./configure \
        --prefix=/ \
        --exec-prefix=/usr/local \
        --includedir=/usr/local/include \
        --datarootdir=/usr/local/share \
        --disable-debug \
        --with-cares \
        --with-openssl \
        --with-pam \
    ; \
    make; \
    make install; \
    rm -rf \
        /usr/local/lib/perl* \
        /usr/local/share/perl* \
    ;

################################################################################

FROM base
ARG TARGETPLATFORM

RUN \
    --mount=type=cache,id=$TARGETPLATFORM:/var/cache/apk,target=/var/cache/apk,sharing=locked \
    --mount=type=cache,target=$TARGETPLATFORM:/var/cache,sharing=locked \
    --mount=type=tmpfs,target=/var/log \
    set -eux; \
    apk add \
        tini \
    ;

RUN set -eux; \
    adduser -SHD pgbouncer;

COPY --link --from=builder /usr/local /usr/local

# Smoke test
RUN set -eux; \
    /usr/local/bin/pgbouncer --help;

USER pgbouncer
ENV PATH /usr/local/bin:$PATH
ENTRYPOINT ["/sbin/tini", "--"]
STOPSIGNAL SIGINT
EXPOSE 6432

CMD ["/usr/local/bin/pgbouncer"]
