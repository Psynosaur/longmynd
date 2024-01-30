#!/bin/bash

#Require sudo
if [ $EUID != 0 ]; then
    sudo "$0" "$@"
    exit $?
fi

echo "removing service..."
systemctl stop longmynd
systemctl disable longmynd
echo "done"

echo "removing service from /etc/systemd/system/..."
rm /etc/systemd/system/longmynd.service
echo "done"

echo "reloading services"
systemctl daemon-reload
echo "done"

echo "Longmynd uninstalled successfully!"
echo "Huzzah"
