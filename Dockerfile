FROM alpine AS base
ARG TARGETPLATFORM

RUN \
    --mount=type=cache,id=$TARGETPLATFORM:/var/cache/apk,target=/var/cache/apk,sharing=locked \
    --mount=type=cache,target=$TARGETPLATFORM:/var/cache,sharing=locked \
    --mount=type=tmpfs,target=/var/log \
    set -eux; \
    apk adduser \
        ca-certificates \
        c-ares \
        libevent \
        openssl \
        readline \
    ;

################################################################################

FROM base AS builder-base
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
        git \
        libevent-dev \
        libtool \
        linux-headers \
        m4 \
        openssl-dev \
        pandoc-cli \
        pkgconfig \
        readline-dev \
    ;

################################################################################

FROM builder-base AS pgbouncer-builder

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
        --with-cares \
    ; \
    make; \
    make install; \
    rm -rf \
        /usr/local/lib/perl* \
        /usr/local/share/perl* \
    ;

################################################################################

FROM builder-base AS psql-builder

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
        --without-zlib \
        --with-openssl \
    ; \
    make -C src/bin install; \
    make -C src/interfaces install; \
    make -C doc install; \
    rm -rf \
        /usr/local/bin/initdb \
        /usr/local/bin/pg_ctl \
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

COPY --link --from=psql-builder /usr/local /usr/local
COPY --link --from=pgbouncer-builder /usr/local /usr/local

# Smoke test
RUN set -eux; \
    /usr/local/bin/pgbouncer --help; \
    /usr/local/bin/psql --help;

USER pgbouncer
ENV PATH /usr/local/bin:$PATH
ENTRYPOINT ["/sbin/tini", "--"]
STOPSIGNAL SIGINT
EXPOSE 6432

CMD ["/usr/local/bin/pgbouncer"]
