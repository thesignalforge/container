# Dockerfile for building and testing Signalforge Container extension
ARG PHP_VERSION=8.3

FROM php:${PHP_VERSION}-cli-alpine

# Install build dependencies
RUN apk add --no-cache \
    autoconf g++ gcc make pkgconf re2c linux-headers ${PHPIZE_DEPS}

WORKDIR /build

# Copy extension source
COPY . /build

# Build the extension
RUN phpize \
    && ./configure --enable-signalforge-container \
    && make \
    && make install \
    && docker-php-ext-enable signalforge_container

# Get run-tests.php from PHP source
RUN wget -q -O /opt/run-tests.php https://raw.githubusercontent.com/php/php-src/master/run-tests.php

# Verify extension is loaded
RUN php -m | grep signalforge_container

WORKDIR /ext
CMD ["php", "-v"]
