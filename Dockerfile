FROM alpine AS base

RUN \
    --mount=type=cache,target=/var/cache,sharing=locked \
    --mount=type=tmpfs,target=/var/log \
    set -eux; \
    apk add --no-cache \
        ca-certificates \
        c-ares \
        libevent \
        linux-pam \
        openssl \
        readline \
    ;

################################################################################

FROM base AS builder-base

RUN \
    --mount=type=cache,target=/var/cache,sharing=locked \
    --mount=type=tmpfs,target=/var/log \
    set -eux; \
    apk add --no-cache \
        autoconf \
        automake \
        build-base \
        libtool \
        linux-headers \
        linux-pam-dev \
        openssl-dev \
        pkgconfig \
    ;

################################################################################

FROM builder-base AS pgbouncer-builder

RUN \
    --mount=type=cache,target=/var/cache,sharing=locked \
    --mount=type=tmpfs,target=/var/log \
    set -eux; \
    apk add --no-cache \
        c-ares-dev \
        git \
        libevent-dev \
        m4 \
        pandoc-cli \
    ;

WORKDIR /opt/pgbouncer
COPY --link . /opt/pgbouncer

RUN set -eux; \
    git submodule update --init; \
    ./autogen.sh; \
    ./configure \
        --prefix=/ \
        --exec-prefix=/usr/local \
        --includedir=/usr/local/include \
        --datarootdir=/usr/local/share \
        --disable-evdns \
        --with-pam \
        --with-cares \
    ; \
    make; \
    make install; \
    rm -rf \
        /usr/local/share/perl* \
    ;

################################################################################

FROM builder-base AS psql-builder

RUN \
    --mount=type=cache,target=/var/cache,sharing=locked \
    --mount=type=tmpfs,target=/var/log \
    set -eux; \
    apk add --no-cache \
        readline-dev \
    ;

WORKDIR /opt/postgresql

RUN set -eux; \
    wget -q https://ftp.postgresql.org/pub/source/v16.1/postgresql-16.1.tar.bz2; \
    wget -q https://ftp.postgresql.org/pub/source/v16.1/postgresql-16.1.tar.bz2.sha256; \
    sha256sum -c postgresql-16.1.tar.bz2.sha256; \
    tar -xjf postgresql-16.1.tar.bz2 --strip-components=1; \
    ./configure \
        --prefix=/ \
        --exec-prefix=/usr/local \
        --includedir=/usr/local/include \
        --datarootdir=/usr/local/share \
        --without-icu \
        --with-pam \
        --without-zlib \
    ; \
    make -C src/bin install; \
    make -C src/interfaces install; \
    make -C doc install; \
    rm -rf \
        /usr/local/include \
        /usr/local/lib/*.a \
        /usr/local/lib/perl* \
        /usr/local/lib/pkgconfig \
        /usr/local/share/doc \
        /usr/local/share/perl* \
        /usr/local/share/postgresql \
    ;

################################################################################

FROM base

RUN \
    --mount=type=cache,target=/var/cache,sharing=locked \
    --mount=type=tmpfs,target=/var/log \
    set -eux; \
    apk add --no-cache \
        tini \
    ;

RUN set -eux; \
    adduser -SHD pgbouncer;

COPY --link --from=psql-builder /usr/local /usr/local
COPY --link --from=pgbouncer-builder /usr/local /usr/local

# Smoke test
RUN set -eux; \
    /usr/local/bin/pgbouncer --help; \
    /usr/local/bin/psql --help;

USER pgbouncer
ENTRYPOINT ["/sbin/tini", "--"]
STOPSIGNAL SIGINT
EXPOSE 6432

CMD ["/usr/local/bin/pgbouncer"]
