#!/bin/bash

module="scull"
device="$module"

rm -f /dev/${device}[0-3]

/sbin/rmmod $module
