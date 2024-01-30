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

pwd=${pwd}
sed -i "s?placeholder?$PWD?g" longmynd.service
sed -i "s?suchuser?$SUDO_USER?g" longmynd.service
sed -i "s?pluto?$1?g" longmynd.service

echo "adding service to /lib/systemd/system/..."
cp longmynd.service /etc/systemd/system/
chmod 644 /etc/systemd/system/longmynd.service
echo "done"

echo "starting and enabling service..."
systemctl daemon-reload
systemctl enable longmynd
systemctl start longmynd
echo "done"

echo "Longmynd installed successfully!"
echo ""
echo "log output can be viewed by running"
echo "sudo journalctl -u longmynd.service -f -n"
git update-index --assume-unchanged *.sh
git update-index --assume-unchanged *.service
