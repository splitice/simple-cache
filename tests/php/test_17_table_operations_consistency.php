<?php
/**
 * test_17_table_operations_consistency.php
 * Table-level operation consistency.
 *
 * Tests:
 * - PUT multiple keys, GET table listing, verify all present
 * - PUT keys, start async GET on one key, DELETE table, verify GET completes
 * - BULK delete individual keys, verify remaining keys intact
 */

require_once __DIR__ . '/DataConsistencyHelper.php';

$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);

$allPassed = true;

// ============================================================
// PUT multiple keys, GET table listing
// ============================================================
testHeader('Table listing: PUT multiple keys, verify listing');

$keys = ['alpha', 'beta', 'gamma', 'delta', 'epsilon'];
$contents = [];

foreach ($keys as $key) {
    $contents[$key] = generateKnownContent(50);
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    assertOrDie($sock !== false, "Could not connect: $errstr");
    $request = "PUT /t17_listing/$key HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: 50\r\n\r\n" . $contents[$key];
    fwrite($sock, $request);
    $response = '';
    stream_set_timeout($sock, 3);
    while (!feof($sock)) {
        $data = @fread($sock, 4096);
        if ($data === false || $data === '') break;
        $response .= $data;
    }
    fclose($sock);
    
    if (strpos($response, '200 OK') === false) {
        testResult("PUT $key", false);
        $allPassed = false;
    }
}

// GET table listing
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t17_listing HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 5);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

$passed = strpos($response, '200 OK') !== false;
testResult('Table listing returns 200', $passed);
$allPassed = $allPassed && $passed;

// Check that all keys appear in listing
$bodyStart = strpos($response, "\r\n\r\n");
$body = $bodyStart !== false ? substr($response, $bodyStart + 4) : '';
$allFound = true;
foreach ($keys as $key) {
    if (strpos($body, $key) === false) {
        echo "  Key '$key' not found in listing\n";
        $allFound = false;
    }
}
$passed = $allFound;
testResult('All keys present in table listing', $passed);
$allPassed = $allPassed && $passed;

// Verify each key's content
foreach ($keys as $key) {
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    $request = "GET /t17_listing/$key HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
    fwrite($sock, $request);
    $response = '';
    stream_set_timeout($sock, 3);
    while (!feof($sock)) {
        $data = @fread($sock, 4096);
        if ($data === false || $data === '') break;
        $response .= $data;
    }
    fclose($sock);
    $bodyStart = strpos($response, "\r\n\r\n");
    $body = $bodyStart !== false ? substr($response, $bodyStart + 4) : '';
    $passed = verifyContent($body, $contents[$key], "GET $key");
    if (!$passed) {
        testResult("Verify $key content", false);
        $allPassed = false;
    }
}

// ============================================================
// PUT keys, async GET on one key, DELETE table, verify GET completes
// ============================================================
testHeader('Table delete: Async GET survives table DELETE');

$content = generateKnownContent(SMALL_SIZE);

// PUT key
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t17_tabledel/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($content) . "\r\n\r\n$content";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);
$passed = strpos($response, '200 OK') !== false;
testResult('PUT succeeds', $passed);
$allPassed = $allPassed && $passed;

// Start async GET
$getInfo = startAsyncGet($host, $port, 't17_tabledel', 'k1');

// DELETE entire table while GET in progress
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "DELETE /t17_tabledel HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);
$passed = strpos($response, '200 OK') !== false || strpos($response, 'DELETED') !== false;
testResult('Table DELETE succeeds', $passed);
$allPassed = $allPassed && $passed;

// GET should complete with full content
$getResult = waitAsyncGet($getInfo);
$passed = verifyContent($getResult, $content, 'GET during table delete');
testResult('GET completes with full content despite table delete', $passed);
$allPassed = $allPassed && $passed;

// Subsequent GET should return 404 (table deleted)
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t17_tabledel/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);
$passed = strpos($response, '404') !== false || strpos($response, 'Not Found') !== false;
testResult('Subsequent GET after table delete returns 404', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// BULK delete individual keys, verify remaining keys intact
// ============================================================
testHeader('BULK delete: Remove some keys, verify others intact');

// PUT several keys
$bulkKeys = ['bulk_a', 'bulk_b', 'bulk_c', 'bulk_d', 'bulk_e'];
$bulkContents = [];
foreach ($bulkKeys as $key) {
    $bulkContents[$key] = generateKnownContent(50);
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    $request = "PUT /t17_bulk/$key HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: 50\r\n\r\n" . $bulkContents[$key];
    fwrite($sock, $request);
    $response = '';
    stream_set_timeout($sock, 3);
    while (!feof($sock)) {
        $data = @fread($sock, 4096);
        if ($data === false || $data === '') break;
        $response .= $data;
    }
    fclose($sock);
}

// BULK delete bulk_a and bulk_c
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "BULK /t17_bulk HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nX-Delete: bulk_a\r\nX-Delete: bulk_c\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);
$passed = strpos($response, '200 OK') !== false || strpos($response, 'BULK OK') !== false;
testResult('BULK delete succeeds', $passed);
$allPassed = $allPassed && $passed;

// Verify deleted keys are gone
foreach (['bulk_a', 'bulk_c'] as $key) {
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    $request = "GET /t17_bulk/$key HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
    fwrite($sock, $request);
    $response = '';
    stream_set_timeout($sock, 3);
    while (!feof($sock)) {
        $data = @fread($sock, 4096);
        if ($data === false || $data === '') break;
        $response .= $data;
    }
    fclose($sock);
    $passed = strpos($response, '404') !== false || strpos($response, 'Not Found') !== false;
    testResult("BULK-deleted key '$key' returns 404", $passed);
    $allPassed = $allPassed && $passed;
}

// Verify remaining keys are intact
foreach (['bulk_b', 'bulk_d', 'bulk_e'] as $key) {
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    $request = "GET /t17_bulk/$key HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
    fwrite($sock, $request);
    $response = '';
    stream_set_timeout($sock, 3);
    while (!feof($sock)) {
        $data = @fread($sock, 4096);
        if ($data === false || $data === '') break;
        $response .= $data;
    }
    fclose($sock);
    $bodyStart = strpos($response, "\r\n\r\n");
    $body = $bodyStart !== false ? substr($response, $bodyStart + 4) : '';
    $passed = verifyContent($body, $bulkContents[$key], "GET $key after BULK delete");
    testResult("Non-deleted key '$key' still intact", $passed);
    $allPassed = $allPassed && $passed;
}

// ============================================================
// Table listing with X-Start and X-Limit
// ============================================================
testHeader('Table listing: X-Start and X-Limit pagination');

// PUT 10 keys in a new table
for ($i = 0; $i < 10; $i++) {
    $content = generateKnownContent(20);
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    $request = "PUT /t17_paginate/k$i HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: 20\r\n\r\n$content";
    fwrite($sock, $request);
    $response = '';
    stream_set_timeout($sock, 3);
    while (!feof($sock)) {
        $data = @fread($sock, 4096);
        if ($data === false || $data === '') break;
        $response .= $data;
    }
    fclose($sock);
}

// GET with X-Limit: 3
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t17_paginate HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nX-Limit: 3\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

$passed = strpos($response, '200 OK') !== false;
testResult('Table listing with X-Limit returns 200', $passed);
$allPassed = $allPassed && $passed;

// Count entries in response body
$bodyStart = strpos($response, "\r\n\r\n");
$body = $bodyStart !== false ? substr($response, $bodyStart + 4) : '';
$lines = array_filter(explode("\n", trim($body)));
$passed = count($lines) <= 3;
testResult('X-Limit:3 returns at most 3 entries', $passed);
$allPassed = $allPassed && $passed;

echo "\n";
if ($allPassed) {
    echo "test_17_table_operations_consistency: ALL TESTS PASSED\n";
    exit(0);
} else {
    echo "test_17_table_operations_consistency: SOME TESTS FAILED\n";
    exit(1);
}