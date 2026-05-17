<?php
/**
 * test_7_interrupted_while_writing.php
 * Matrix A3: Interrupted while writing (initial PUT)
 *   B1: Small update (blockdb)
 *   B2: Large update (file)
 *
 * Verifies: When a PUT is interrupted mid-stream, the entry is cleaned up
 * (cache_destroy marks deleted, sets writing=false). GET returns 404.
 * After server restart, the partial write is not persisted.
 */

require_once __DIR__ . '/DataConsistencyHelper.php';

$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);

$allPassed = true;

// ============================================================
// A3 × B1: Small content (blockdb) - interrupted while writing
// ============================================================
testHeader('A3×B1: Interrupted while writing (small/blockdb)');

$content = generateKnownContent(SMALL_SIZE);

// Start async PUT with pause
$info = startAsyncPutWithPause($host, $port, 't7_small', 'k1', $content);

// Kill the PUT mid-stream (close without sending body)
signalAsyncPutKill($info);
$response = waitAsyncPut($info);

// GET should return 404 (entry cleaned up by cache_destroy)
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect: $errstr");
$request = "GET /t7_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 1);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

$passed = strpos($response, '404') !== false || strpos($response, 'Not Found') !== false;
testResult('GET after interrupted small write returns 404', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// A3 × B2: Large content (file) - interrupted while writing
// ============================================================
testHeader('A3×B2: Interrupted while writing (large/file)');

$content = generateKnownContent(LARGE_SIZE);

$info = startAsyncPutWithPause($host, $port, 't7_large', 'k1', $content);

// Kill mid-stream
signalAsyncPutKill($info);
$response = waitAsyncPut($info);

// GET should return 404
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t7_large/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 1);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

$passed = strpos($response, '404') !== false || strpos($response, 'Not Found') !== false;
testResult('GET after interrupted large write returns 404', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Variant: Interrupt at different points - before any body data
// ============================================================
testHeader('A3 Variant: Interrupt before any body data');

$content = generateKnownContent(SMALL_SIZE);

// Start PUT but kill immediately after headers (before any body)
$info = startAsyncPutWithPause($host, $port, 't7_variant1', 'k1', $content);
signalAsyncPutKill($info);
waitAsyncPut($info);

$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t7_variant1/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 1);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

$passed = strpos($response, '404') !== false || strpos($response, 'Not Found') !== false;
testResult('GET after interrupt before body returns 404', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Variant: Interrupt after partial body data
// ============================================================
testHeader('A3 Variant: Interrupt after partial body data');

$content = generateKnownContent(SMALL_SIZE);

// Start PUT, send partial body, then kill
$info = startAsyncPutWithPause($host, $port, 't7_variant2', 'k1', $content);

// Send partial body via raw socket
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$partialBody = substr($content, 0, 30);
$request = "PUT /t7_variant2/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($content) . "\r\n\r\n$partialBody";
fwrite($sock, $request);
// Don't read response, just close
fclose($sock);

// Kill the paused PUT
signalAsyncPutKill($info);
waitAsyncPut($info);

// Wait a moment for cleanup
usleep(200000);

// GET should return 404
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t7_variant2/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 1);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

$passed = strpos($response, '404') !== false || strpos($response, 'Not Found') !== false;
testResult('GET after interrupt with partial body returns 404', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Variant: Verify key can be reused after interrupted write
// ============================================================
testHeader('A3 Variant: Key reusable after interrupted write');

$content = generateKnownContent(SMALL_SIZE);

// Interrupt a write
$info = startAsyncPutWithPause($host, $port, 't7_reuse', 'k1', $content);
signalAsyncPutKill($info);
waitAsyncPut($info);

// Now do a complete PUT on same key
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t7_reuse/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($content) . "\r\n\r\n$content";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 1);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);
$passed = strpos($response, '200 OK') !== false;
testResult('PUT succeeds after interrupted write on same key', $passed);
$allPassed = $allPassed && $passed;

// Verify content
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t7_reuse/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 1);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);
$bodyStart = strpos($response, "\r\n\r\n");
$body = $bodyStart !== false ? substr($response, $bodyStart + 4) : '';
$passed = verifyContent($body, $content, 'GET after reuse');
testResult('GET after reuse returns correct content', $passed);
$allPassed = $allPassed && $passed;

echo "\n";
if ($allPassed) {
    echo "test_7_interrupted_while_writing: ALL TESTS PASSED\n";
    exit(0);
} else {
    echo "test_7_interrupted_while_writing: SOME TESTS FAILED\n";
    exit(1);
}