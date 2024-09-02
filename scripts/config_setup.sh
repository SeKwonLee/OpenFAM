#!/bin/bash

#spack repo add /opt/hpe/spack/mochi-spack-packages

. /opt/hpe/spack/spack/share/spack/setup-env.sh

#Load Margo libraries
spack load mochi-margo

#Load Thallium libraries
spack load mochi-thallium

#Swap Compilers
module swap PrgEnv-cray/8.3.3 PrgEnv-gnu/8.3.3
module swap gcc/12.1.0 gcc/11.2.0

#Export all paths
export OPENFAM_ROOT=/home/leesek/projects/OpenFAM/build
