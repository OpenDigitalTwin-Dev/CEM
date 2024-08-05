#!/bin/sh

sudo apt update
sudo apt -y install cmake
sudo apt -y install make
sudo apt -y install gcc g++ gfortran

# need to set MFEM_USE_SUPERLU5=ON, if dont superlu has some problems
mkdir build
cd build
cmake \
    -DMFEM_USE_CEED=ON \
    -DMFEM_USE_MPI=ON \
    -DMFEM_USE_SUPERLU5=ON \
    -DMFEM_USE_METIS=ON \
    -DCEED_DIR=$PWD/../../../NLA/install/libceed_install \
    -DHYPRE_DIR=$PWD/../../../NLA/install/hypre_install \
    -DParMETIS_DIR=$PWD/../../../install/parmetis_install \
    -DSuperLUDist_DIR=$PWD/../../../NLA/install/superlu_install_dist \
    -DSuperLUDist_INCLUDE_DIR=$PWD/../../../NLA/install/superlu_install_dist/include \
    -DSuperLUDist_LIBRARY=$PWD/../../../NLA/install/superlu_install_dist/lib/libsuperlu_dist.a \
    -DMETIS_DIR=$PWD/../../../NSM/extern/ALE/install/metis \
    -DCMAKE_INSTALL_PREFIX=$PWD/../../install/mfem_install .. 
make -j4
make install
