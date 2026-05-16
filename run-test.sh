#!/bin/bash

# run-test.sh - Build and run tests for simple-cache
# Usage: ./run-test.sh [Release|Debug]
# Default: Release

set -e  # Exit on error

# Parse configuration argument (case insensitive)
CONFIG="${1:-Release}"
CONFIG=$(echo "$CONFIG" | tr '[:lower:]' '[:upper:]')

# Validate configuration
if [ "$CONFIG" != "RELEASE" ] && [ "$CONFIG" != "DEBUG" ]; then
    echo "Error: Invalid configuration '$1'"
    echo "Usage: $0 [Release|Debug]"
    exit 1
fi

echo "=========================================="
echo "Building simple-cache in ${CONFIG} mode"
echo "=========================================="

# Clean previous builds
echo "Cleaning previous builds..."
make clean

# Build the project with the specified configuration
echo "Building project (CONFIG=${CONFIG})..."
make CONFIG=${CONFIG}

# Build the tests
echo "Building tests (CONFIG=${CONFIG})..."
make CONFIG=${CONFIG} tests

echo ""
echo "=========================================="
echo "Running tests..."
echo "=========================================="

# Change to tests directory and run tests
cd tests
./tests ../src/server/scache ../testcases
TEST_RESULT=$?

cd ..

echo ""
echo "=========================================="
if [ $TEST_RESULT -eq 0 ]; then
    echo "All tests passed successfully!"
else
    echo "Tests failed with exit code: $TEST_RESULT"
fi
echo "=========================================="

exit $TEST_RESULT
