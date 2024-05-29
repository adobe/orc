#!/bin/bash

cd $(dirname "$0")

VERSION_HEADER=./include/orc/version.hpp
VERSION_STR="$1"
SHA_STR="$2"

echo "#define ORC_VERSION_STR() \"$VERSION_STR\"" > $VERSION_HEADER
echo "#define ORC_SHA_STR() \"$SHA_STR\"" >> $VERSION_HEADER

echo "Version: $VERSION_STR"
echo "SHA: $SHA_STR"
