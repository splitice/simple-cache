<?php
/**
 * test_5_access_while_writing.php
 * Matrix A1: Access while writing (initial PUT)
 *   B1: Small update (blockdb, <=4096 bytes)
 *   B2: Large update (file, >4096 bytes)
 *
 * Verifies: GET during an in-progress PUT returns 404 (writing flag),
 * and GET after completion returns correct content.
 */

require_once __DIR__ . '/DataConsistencyHelper.php';

$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);

$allPassed = true;

// ============================================================
// A1 × B1: Small content (blockdb) - access while writing
// ============================================================
testHeader('A1×B1: Access while writing (small/blockdb)');

$content = generateKnownContent(SMALL_SIZE);

// Start async PUT with pause after headers
$info = startAsyncPutWithPause($host, $port, 't5_small', 'k1', $content);

// While PUT is paused (writing=true), try GET - should get 404
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect: $errstr");

$request = "GET /t5_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('GET during write returns 404', $passed);
$allPassed = $allPassed && $passed;

// Now signal PUT to continue and complete
signalAsyncPutContinue($info);
$putResponse = waitAsyncPut($info);
$passed = strpos($putResponse, '200 OK') !== false;
testResult('PUT completes successfully', $passed);
$allPassed = $allPassed && $passed;

// GET after completion should return correct content
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t5_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $content, 'GET after write');
testResult('GET after write returns correct content', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// A1 × B2: Large content (file) - access while writing
// ============================================================
testHeader('A1×B2: Access while writing (large/file)');

$content = generateKnownContent(LARGE_SIZE);

$info = startAsyncPutWithPause($host, $port, 't5_large', 'k1', $content);

// GET during write - should get 404
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect: $errstr");

$request = "GET /t5_large/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('GET during large write returns 404', $passed);
$allPassed = $allPassed && $passed;

// Complete PUT
signalAsyncPutContinue($info);
$putResponse = waitAsyncPut($info);
$passed = strpos($putResponse, '200 OK') !== false;
testResult('Large PUT completes successfully', $passed);
$allPassed = $allPassed && $passed;

// GET after completion
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t5_large/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 5);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

$bodyStart = strpos($response, "\r\n\r\n");
$body = $bodyStart !== false ? substr($response, $bodyStart + 4) : '';
$passed = verifyContent($body, $content, 'GET after large write');
testResult('GET after large write returns correct content', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Variant: GET different key in same table during write
// ============================================================
testHeader('A1 Variant: GET different key during write');

// Pre-populate another key
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$otherContent = generateKnownContent(50);
$request = "PUT /t5_small/k2 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: 50\r\n\r\n$otherContent";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

// Now start a write on k1
$content3 = generateKnownContent(SMALL_SIZE);
$info = startAsyncPutWithPause($host, $port, 't5_small', 'k1', $content3);

// GET k2 during write on k1 - should succeed normally
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t5_small/k2 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $otherContent, 'GET different key');
testResult('GET different key during write succeeds', $passed);
$allPassed = $allPassed && $passed;

// Cleanup
signalAsyncPutContinue($info);
waitAsyncPut($info);

echo "\n";
if ($allPassed) {
    echo "test_5_access_while_writing: ALL TESTS PASSED\n";
    exit(0);
} else {
    echo "test_5_access_while_writing: SOME TESTS FAILED\n";
    exit(1);
}