#!/bin/bash

cd "$(dirname "$0")" || exit 1

[ -f VERSION ] || { echo "VERSION file not found"; exit 1; }

PRODUCT=taosocks
VERSION=$(head -1 VERSION)

# $1: OS $2: ARCH
build() {
    [ ${#@} -ne 2 ] && { echo "bad arguments."; exit 1; }

    ar_name="$PRODUCT-$1-$2-$VERSION"

    echo "Building $ar_name ..."

    case "$1" in
        windows) EXT=".exe";;
        *) EXT="";;
    esac

    set -e

    rm -rf "$ar_name"
    rm -rf "$ar_name.zip"
    mkdir -p "$ar_name"

    cd server
    GOOS=$1 GOARCH=$2 go build
    cd ..
    cd client
    GOOS=$1 GOARCH=$2 go build
    cd ..

    mkdir -p "$ar_name/server"
    cp "server/server$EXT" "$ar_name/server"

    mkdir -p "$ar_name/client"
    cp "client/client$EXT" "$ar_name/client"

    mkdir -p "$ar_name/config"
    cp "config/rules.txt" "config/server.crt" "config/server.key" "$ar_name/config"

	zip -9 -r "$ar_name.zip" "$ar_name"

	rm -rf "$ar_name"

    set +e

    echo "created $ar_name.zip."
}

build windows 386
build windows amd64
build linux 386
build linux amd64
build darwin 386
build darwin amd64

echo -e "\nAll done."
