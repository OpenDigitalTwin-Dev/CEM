#!/bin/sh

sudo apt update
sudo apt -y install cmake
sudo apt -y install make
sudo apt -y install gcc g++ gfortran libmpich-dev

make -j4
make install
mkdir -p ../install/gslib_install
cp -r include ../install/gslib_install
cp -r lib ../install/gslib_install
