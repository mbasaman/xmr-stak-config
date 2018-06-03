#! /bin/sh

mv xmr-stak-config xmr-stak-config.bak
rm Makefile
rm CMakeCache.txt
rm cmake_install.cmake
rm -r CMakeFiles

cmake . || exit 1
make || exit 1

echo "done"
