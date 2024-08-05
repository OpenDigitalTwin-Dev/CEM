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
    -DMFEM_USE_SUPERLU=ON \
    -DMFEM_USE_METIS=ON \
    -DCEED_DIR=$PWD/../../../NLA/install/libceed_install \
    -DHYPRE_DIR=$PWD/../../../NLA/install/hypre_install \
    -DParMETIS_DIR=$PWD/../../../install/parmetis_install \
    -DMETIS_DIR=$PWD/../../../NSM/extern/ALE/install/metis \
    -DSuperLUDist_DIR=$PWD/../../../NLA/install/superlu_dist_install \
    -DSuperLUDist_INCLUDE_DIR=$PWD/../../../NLA/install/superlu_dist_install/include \
    -DSuperLUDist_LIBRARY=$PWD/../../../NLA/install/superlu_dist_install/lib/libsuperlu_dist.a \
    -DCMAKE_INSTALL_PREFIX=$PWD/../../install/mfem_install .. 
make -j4
make install


