<?php
/**
 * test_8_interrupted_while_replacing.php
 * Matrix A4: Interrupted while replacing (PUT on existing key)
 *   B1: Small update (blockdb)
 *   B2: Large update (file)
 *
 * NOTE: The old entry is soft-deleted in db_entry_get_write BEFORE the new write
 * begins. If the new write is interrupted, the key is effectively lost.
 * This test documents current behavior.
 */

require_once __DIR__ . '/DataConsistencyHelper.php';

$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);

$allPassed = true;

// ============================================================
// A4 × B1: Small content (blockdb) - interrupted while replacing
// ============================================================
testHeader('A4×B1: Interrupted while replacing (small/blockdb)');

$originalContent = generateKnownContent(SMALL_SIZE);
$replacementContent = generateKnownContent(SMALL_SIZE + 50);

// PUT original
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect: $errstr");
$request = "PUT /t8_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($originalContent) . "\r\n\r\n$originalContent";
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
testResult('PUT original succeeds', $passed);
$allPassed = $allPassed && $passed;

// Start async replacement, then kill mid-stream
$info = startAsyncPutWithPause($host, $port, 't8_small', 'k1', $replacementContent);
signalAsyncPutKill($info);
waitAsyncPut($info);

// GET - the old entry was soft-deleted, new never completed → key is lost
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t8_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

// Current behavior: key is lost (404) because old entry was soft-deleted
// before the new write began, and the new write was interrupted.
$is404 = strpos($response, '404') !== false || strpos($response, 'Not Found') !== false;
echo "  NOTE: Current behavior after interrupted replace: key is " . ($is404 ? "lost (404)" : "still present") . "\n";
$passed = $is404; // Document current behavior
testResult('GET after interrupted replace returns 404 (key lost)', $passed);
$allPassed = $allPassed && $passed;

// Verify key can be re-created
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$newContent = generateKnownContent(SMALL_SIZE);
$request = "PUT /t8_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($newContent) . "\r\n\r\n$newContent";
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
testResult('Key can be re-created after interrupted replace', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// A4 × B2: Large content (file) - interrupted while replacing
// ============================================================
testHeader('A4×B2: Interrupted while replacing (large/file)');

$originalContent = generateKnownContent(LARGE_SIZE);
$replacementContent = generateKnownContent(LARGE_SIZE + 100);

// PUT original
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t8_large/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($originalContent) . "\r\n\r\n$originalContent";
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
testResult('PUT large original succeeds', $passed);
$allPassed = $allPassed && $passed;

// Start async replacement, kill mid-stream
$info = startAsyncPutWithPause($host, $port, 't8_large', 'k1', $replacementContent);
signalAsyncPutKill($info);
waitAsyncPut($info);

// GET - key should be lost
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t8_large/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

$is404 = strpos($response, '404') !== false || strpos($response, 'Not Found') !== false;
echo "  NOTE: Current behavior after interrupted large replace: key is " . ($is404 ? "lost (404)" : "still present") . "\n";
$passed = $is404;
testResult('GET after interrupted large replace returns 404 (key lost)', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Variant: Interrupt before Content-Length header
// ============================================================
testHeader('A4 Variant: Interrupt before Content-Length header');

$originalContent = generateKnownContent(SMALL_SIZE);

// PUT original
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t8_variant/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($originalContent) . "\r\n\r\n$originalContent";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

// Start replacement but send incomplete headers (no Content-Length)
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t8_variant/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n";
fwrite($sock, $request);
// Close without completing headers
fclose($sock);

// Wait a moment
usleep(200000);

// GET - original should still exist (replacement never started properly)
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t8_variant/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $originalContent, 'GET after incomplete header replacement');
testResult('Original content preserved after incomplete header replacement', $passed);
$allPassed = $allPassed && $passed;

echo "\n";
if ($allPassed) {
    echo "test_8_interrupted_while_replacing: ALL TESTS PASSED\n";
    exit(0);
} else {
    echo "test_8_interrupted_while_replacing: SOME TESTS FAILED\n";
    exit(1);
}