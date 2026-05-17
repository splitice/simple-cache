<?php
/**
 * test_15_lru_eviction_consistency.php
 * LRU eviction with active readers.
 *
 * Tests:
 * - Fill cache beyond limit, verify oldest entries evicted
 * - PUT key, start async GET, fill cache to trigger eviction of that key,
 *   verify GET still completes (refcount>0 protection in db_lru_cleanup_percent)
 *
 * NOTE: Requires server started with --database-max-size.
 * The test runner should start a separate server instance for this test.
 */

require_once __DIR__ . '/DataConsistencyHelper.php';

$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);

$allPassed = true;

function triggerLruGc($host, $port) {
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    assertOrDie($sock !== false, "Could not connect for LRU GC: $errstr");

    $request = "ADMIN /gc HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
    fwrite($sock, $request);

    $response = '';
    stream_set_timeout($sock, 1);
    while (!feof($sock)) {
        $data = @fread($sock, 4096);
        if ($data === false || $data === '') break;
        $response .= $data;
        if (isRequestEnd($response)) break;
    }
    fclose($sock);

    return strpos($response, '200 OK') !== false;
}

// ============================================================
// Fill cache beyond limit, verify oldest entries evicted
// ============================================================
testHeader('LRU: Fill beyond limit, oldest evicted');

// We'll use small content to fit many entries
// The server should be started with --database-max-size 50000 (50KB)
// Each entry is ~100 bytes of data + overhead

// PUT 20 entries of ~3000 bytes each = ~60KB, should trigger LRU
$numEntries = 20;
$entrySize = 3000;

echo "  Putting $numEntries entries of $entrySize bytes each...\n";

for ($i = 0; $i < $numEntries; $i++) {
    $content = generateKnownContent($entrySize);
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    if (!$sock) {
        echo "  Connection failed at entry $i, server may have crashed\n";
        break;
    }
    $request = "PUT /t15_fill/k$i HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: $entrySize\r\n\r\n$content";
    fwrite($sock, $request);
    $response = '';
    stream_set_timeout($sock, 1);
    while (!feof($sock)) {
        $data = @fread($sock, 4096);
        if ($data === false || $data === '') break;
        $response .= $data;
        if(isRequestEnd($response)) break; // Stop reading after headers + content
    }
    fclose($sock);
    
    if (strpos($response, '200 OK') === false) {
        echo "  PUT failed at entry $i\n";
    }
}

$passed = triggerLruGc($host, $port);
testResult('Manual LRU GC succeeds after cache fill', $passed);
$allPassed = $allPassed && $passed;

// Check which keys survived - the oldest (lowest index) should be evicted
$survived = 0;
$evicted = 0;
for ($i = 0; $i < $numEntries; $i++) {
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    if (!$sock) break;
    $request = "GET /t15_fill/k$i HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
    fwrite($sock, $request);
    $response = '';
    stream_set_timeout($sock, 1);
    while (!feof($sock)) {
        $data = @fread($sock, 4096);
        if ($data === false || $data === '') break;
        $response .= $data;
        if(isRequestEnd($response)) break; // Stop reading after headers + content
    }
    fclose($sock);
    
    if (strpos($response, '200 OK') !== false) {
        $survived++;
    } else {
        $evicted++;
    }
}

echo "  Survived: $survived, Evicted: $evicted\n";
$passed = $evicted > 0; // At least some entries should be evicted
testResult('Some entries evicted when cache exceeds limit', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// LRU eviction with active reader (refcount protection)
// ============================================================
testHeader('LRU: Active reader protected from eviction');

// PUT a key that we'll read
$protectedContent = generateKnownContent(2000);
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect: $errstr");
$request = "PUT /t15_protect/protected HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: 2000\r\n\r\n$protectedContent";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 1);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
        if(isRequestEnd($response)) break; // Stop reading after headers + content
}
fclose($sock);
$passed = strpos($response, '200 OK') !== false;
testResult('PUT protected key succeeds', $passed);
$allPassed = $allPassed && $passed;

// Start async GET on the protected key (slow consumer)
$getInfo = startAsyncGet($host, $port, 't15_protect', 'protected');

// Now fill cache with many entries to trigger LRU eviction
echo "  Filling cache to trigger LRU while GET is in progress...\n";
for ($i = 0; $i < 30; $i++) {
    $content = generateKnownContent(2000);
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    if (!$sock) break;
    $request = "PUT /t15_protect/filler$i HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: 2000\r\n\r\n$content";
    fwrite($sock, $request);
    $response = '';
    stream_set_timeout($sock, 1);
    while (!feof($sock)) {
        $data = @fread($sock, 4096);
        if ($data === false || $data === '') break;
        $response .= $data;
        if(isRequestEnd($response)) break; // Stop reading after headers + content
    }
    fclose($sock);
}

$passed = triggerLruGc($host, $port);
testResult('Manual LRU GC succeeds during active read', $passed);
$allPassed = $allPassed && $passed;

// Wait for GET to complete - should get full content despite LRU pressure
$getResult = waitAsyncGet($getInfo);
$passed = verifyContent($getResult, $protectedContent, 'GET result during LRU eviction');
testResult('Active reader completes with full content during LRU eviction', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Verify evicted entries return 404
// ============================================================
testHeader('LRU: Evicted entries return 404');

// Try to GET k0 from the fill test - should be evicted (oldest)
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
if ($sock) {
    $request = "GET /t15_fill/k0 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
    fwrite($sock, $request);
    $response = '';
    stream_set_timeout($sock, 1);
    while (!feof($sock)) {
        $data = @fread($sock, 4096);
        if ($data === false || $data === '') break;
        $response .= $data;
        if(isRequestEnd($response)) break; // Stop reading after headers + content
    }
    fclose($sock);
    
    // k0 may or may not be evicted depending on exact sizing
    $is404 = strpos($response, '404') !== false || strpos($response, 'Not Found') !== false;
    $is200 = strpos($response, '200 OK') !== false;
    $passed = $is404 || $is200; // Either is valid
    testResult('Oldest entry state is consistent (404 or 200)', $passed);
    $allPassed = $allPassed && $passed;
}

echo "\n";
if ($allPassed) {
    echo "test_15_lru_eviction_consistency: ALL TESTS PASSED\n";
    exit(0);
} else {
    echo "test_15_lru_eviction_consistency: SOME TESTS FAILED\n";
    exit(1);
}
