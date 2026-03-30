#!/bin/bash
mkdir -p /mnt/data
sudo mergerfs -o allow_other,use_ino data:wesnothlite/test_data /mnt/data
build/wl-cli --data=/mnt/data "$@"