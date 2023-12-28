#!/bin/bash

#Require sudo
if [ $EUID != 0 ]; then
    sudo "$0" "$@"
    exit $?
fi

apt install -y make gcc libusb-1.0-0-dev libasound2-dev libcivetweb-dev
cp minitiouner.rules /etc/udev/rules.d/