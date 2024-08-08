sudo apt update
sudo apt -y install cmake make gcc g++ libmpich-dev liblapack-dev libeigen3-dev

cd palace
rm -rf build
mkdir build
cd build
cmake .. \
      -Dnlohmann_json_DIR=$PWD/../../../../install/json_install/share/cmake/nlohmann_json \
      -Dfmt_DIR=$PWD/../../../../install/fmt_install/lib/cmake/fmt \
      -DMFEM_DIR=$PWD/../../../../CEM/install/mfem_install/lib/cmake/mfem \
      -DLIBCEED_DIR=$PWD/../../../../NLA/install/libceed_install \
      -DPETSC_DIR=$PWD/../../../../NLA/install/petsc_install \
      -DSLEPC_DIR=$PWD/../../../../NLA/install/slepc_install \
      -DPALACE_WITH_OPENMP=ON \
      -DCMAKE_INSTALL_PREFIX=$PWD/../../../install/palace_install 

make -j4
make install

# this command need more time, add_subdirectory(../test/unit ${CMAKE_BINARY_DIR}/test/unit EXCLUDE_FROM_ALL)
