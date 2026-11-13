#!/bin/sh
# essai hors matériel de hidrd.c et gesture.c (i2chid), dans WSL
set -e
cd "$(dirname "$0")"
mkdir -p ../../../../build/i2chid
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -o ../../../../build/i2chid/hidrd_test hidrd_test.c ../hidrd.c ../gesture.c
../../../../build/i2chid/hidrd_test
