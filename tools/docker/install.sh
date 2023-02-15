#!/bin/bash

# Copyright (c) 2021,2022 The University of British Columbia.
# All rights reserved.
#
# This file is distributed under the terms in the attached LICENSE file.

set -e

# update the repositories
apt-get update && apt-get upgrade -y

# install some system dependencies
DEBIAN_FRONTEND=noninteractive apt-get install -y \
    python3 parted wget mtools

# install barrelfish dependencies
apt-get update && apt-get upgrade
DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential bison flex ghc libghc-src-exts-dev \
    libghc-ghc-paths-dev libghc-parsec3-dev libghc-random-dev\
    libghc-ghc-mtl-dev libghc-async-dev picocom cabal-install \
    git gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
    qemu-efi-aarch64 qemu-system-arm qemu-utils

# libelf
#DEBIAN_FRONTEND=noninteractive apt-get install -y \
#  freebsd-glue libelf-freebsd-dev

#
# Work around to get the libelf stuff going right now...
#
DEBIAN_FRONTEND=noninteractive apt-get install -y \
        libbsd0 byacc libbsd-dev libdb-dev libexpat-dev \
        libgdbm-dev original-awk zlib1g-dev

wget    http://archive.ubuntu.com/ubuntu/pool/universe/f/freebsd-glue/libfreebsd-glue-0_0.2.22_amd64.deb
dpkg -i libfreebsd-glue-0_0.2.22_amd64.deb
wget 	http://archive.ubuntu.com/ubuntu/pool/universe/f/freebsd-glue/freebsd-glue_0.2.22_amd64.deb
dpkg -i freebsd-glue_0.2.22_amd64.deb
wget 	http://archive.ubuntu.com/ubuntu/pool/universe/f/freebsd-libs/libelf-freebsd-1_10.3~svn296373-10_amd64.deb
dpkg -i libelf-freebsd-1_10.3~svn296373-10_amd64.deb
wget    http://archive.ubuntu.com/ubuntu/pool/universe/f/freebsd-libs/libelf-freebsd-dev_10.3~svn296373-10_amd64.deb
dpkg -i libelf-freebsd-dev_10.3~svn296373-10_amd64.deb


# install the remaining haskell package
cabal v1-update
cabal v1-install --global bytestring-trie

# install the autograder dependencies
DEBIAN_FRONTEND=noninteractive apt-get install -y \
    python3 python3-pexpect  python3-plumbum

# get the uuu tool
wget -P /bin https://github.com/NXPmicro/mfgtools/releases/download/uuu_1.4.165/uuu
chmod 755 /bin/uuu

# create the tools directory
mkdir /source
chmod 755 /source

# clean the apt
apt-get clean && apt-get autoclean && apt-get autoremove -y

# make sure it's executable
chmod 755 /entrypoint.sh
