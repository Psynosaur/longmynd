#!/bin/bash
# Require pluto broker address
if [[ $# -eq 0 ]] ; then
    echo 'No pluto broker address provided'
    exit 1
fi
#Require sudo
if [ $EUID != 0 ]; then
    sudo "$0" "$@"
    exit $?
fi

git checkout .
git pull 
chmod +x *.sh 
make clean 
make 
./uninstall.sh
./install_longmynd.sh $1