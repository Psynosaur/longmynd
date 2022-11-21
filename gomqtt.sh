#! /bin/bash
# Change  192.168.2.1 to your mqtt broker address
# this command use 192.168.2.1:1883 as the mqtt broker and send TS/UDP or bbframe/udp on multicast 230.0.0.1
./longmynd -p h -M 192.168.2.1 1883 -i 230.0.0.1 1234 1255000 1000
 


