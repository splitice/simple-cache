#!/bin/bash

set -e
set -x

PWD=$(pwd)

while [[ 1 ]]; do
	 ./tests/tests $PWD/src/server/scache $PWD/testcases/
done
