#!/bin/bash

#Require sudo
if [ $EUID != 0 ]; then
    sudo "$0" "$@"
    exit $?
fi

apt install -y make gcc libusb-1.0-0-dev libasound2-dev libmosquitto-dev zlib1g-dev
cp minitiouner.rules /etc/udev/rules.d/
wget https://github.com/civetweb/civetweb/archive/refs/tags/v1.16.tar.gz
tar -xvzf v1.16.tar.gz civetweb-1.16/ 
cd civetweb-1.16
make build WITH_CPP=1 
sudo make install-lib WITH_CPP=1
sudo make install-headers WITH_CPP=1
cd ..
rm -rf civetweb-1.16
