#!/bin/bash
# run-consistency-tests.sh - Run PHP data consistency tests for simple-cache
# Usage: ./run-consistency-tests.sh [port]
# Default port: 8081

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PORT="${1:-8081}"
HOST="127.0.0.1"
PIDFILE="/tmp/scache-consistency-test.pid"
DBDIR="/tmp/scache-consistency-test-db"
SCACHE_BIN="$PROJECT_DIR/src/server/scache"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASSED=0
FAILED=0

echo "=========================================="
echo "simple-cache Data Consistency Tests"
echo "=========================================="
echo "Host: $HOST:$PORT"
echo "DB:   $DBDIR"
echo ""

# Build scache if needed
if [ ! -x "$SCACHE_BIN" ]; then
    echo -e "${YELLOW}Building simple-cache...${NC}"
    cd "$PROJECT_DIR"
    make clean && make
    if [ ! -x "$SCACHE_BIN" ]; then
        echo -e "${RED}Failed to build simple-cache${NC}"
        exit 1
    fi
fi

# Clean up any previous run
cleanup() {
    if [ -f "$PIDFILE" ]; then
        PID=$(cat "$PIDFILE" 2>/dev/null)
        if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
            kill -9 "$PID" 2>/dev/null || true
        fi
        rm -f "$PIDFILE"
    fi
    rm -rf "$DBDIR"
}
cleanup

# Start server
start_server() {
    local extra_args="${1:-}"
    echo -e "${YELLOW}Starting scache on $HOST:$PORT...${NC}"
    rm -rf "$DBDIR"
    mkdir -p "$DBDIR"
    
    # Start scache with correct CLI flags (daemon mode)
    $SCACHE_BIN \
        -r "$DBDIR" \
        -b "$HOST:$PORT" \
        -m "$PIDFILE" \
        -d \
        $extra_args
    
    # Wait for server to be ready (daemon writes real PID after fork)
    for i in $(seq 1 30); do
        if [ -f "$PIDFILE" ]; then
            PID=$(cat "$PIDFILE")
            # PID must be non-zero (daemon parent writes 0, child writes real PID)
            if [ -n "$PID" ] && [ "$PID" -gt 0 ] 2>/dev/null && kill -0 "$PID" 2>/dev/null; then
                sleep 0.5
                # Verify it's listening
                if curl -s -o /dev/null -w "%{http_code}" "http://$HOST:$PORT/" 2>/dev/null | grep -q "200\|404"; then
                    echo -e "${GREEN}Server started (PID: $PID)${NC}"
                    return 0
                fi
            fi
        fi
        sleep 0.5
    done
    
    echo -e "${RED}Failed to start server${NC}"
    return 1
}

# Run a single test
run_test() {
    local test_file="$1"
    local test_name="$(basename "$test_file" .php)"
    
    echo -n "  $test_name ... "
    
    if php "$test_file" "$HOST" "$PORT" 2>&1; then
        echo -e "${GREEN}PASS${NC}"
        PASSED=$((PASSED + 1))
        return 0
    else
        echo -e "${RED}FAIL${NC}"
        FAILED=$((FAILED + 1))
        return 1
    fi
}

# Start server with default config
start_server "" || exit 1

echo ""
echo "Running tests..."
echo ""

# Run all consistency test files
cd "$SCRIPT_DIR"
for test_file in test_5_*.php test_6_*.php test_7_*.php test_8_*.php test_9_*.php test_10_*.php test_11_*.php test_12_*.php test_13_*.php test_14_*.php test_15_*.php test_16_*.php test_17_*.php; do
    if [ -f "$test_file" ]; then
        run_test "$test_file"
    fi
done

echo ""
echo "=========================================="
echo -e "Results: ${GREEN}$PASSED passed${NC}, ${RED}$FAILED failed${NC}"
echo "=========================================="

# Cleanup
cleanup

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
exit 0
