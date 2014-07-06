#!/bin/bash

COMPILE_OPTIONS=""
if [[ "$DEBUG" ]]; then
	COMPILE_OPTIONS="-g"
fi

gcc scache.c db.c hash.c connection.c http.c $COMPILE_OPTIONS -std=c99 -D_POSIX_C_SOURCE=200809L -O3 -o scache