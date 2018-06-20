#!/bin/bash

if [ $# -ne 1 ]; then
  echo "No setup directory specified"
  echo "$(basename $0) <setup-dir>"
  exit 1
fi

SETUP_DIR=$1

mkdir -p ${SETUP_DIR}

SETUP_DIR=$(readlink -f ${SETUP_DIR})
BTOR2TOOLS_DIR=${SETUP_DIR}/btor2tools

NPROC=2
if hash nproc; then
  NPROC=$(nproc)
fi

# Download and build CaDiCaL
git clone --depth 1 https://github.com/Boolector/btor2tools.git ${BTOR2TOOLS_DIR}
cd ${BTOR2TOOLS_DIR}
./configure.sh
make -j${NPROC}