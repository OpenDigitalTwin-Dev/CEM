#!/bin/sh

apt update
apt -y install cmake
apt -y install make
apt -y install gcc g++ gfortran

mkdir build
cd build
cmake \
    -DMFEM_USE_CEED=ON \
    -DCEED_DIR=$PWD/../../../NLA/install/libceed_install \
    -DCMAKE_INSTALL_PREFIX=$PWD/../../install/mfem_install ..
make -j4
make install
