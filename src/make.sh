#!/bin/bash

COMPILE_OPTIONS="-lpcap"

gcc scache.c db.c hash.c $COMPILE_OPTIONS -std=c99 -D_POSIX_C_SOURCE=200809L -O3 -o scache