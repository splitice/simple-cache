<?php
/**
 * test_6_access_while_replacing.php
 * Matrix A2: Access while replacing (PUT on existing key)
 *   B1: Small update (blockdb)
 *   B2: Large update (file)
 *
 * Verifies: GET during replacement returns 404 (old entry soft-deleted, new entry writing=true).
 * After replacement completes, GET returns new content.
 * Also tests size-switch during replacement (small→large, large→small).
 */

require_once __DIR__ . '/DataConsistencyHelper.php';

$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);

$allPassed = true;

// ============================================================
// A2 × B1: Small content (blockdb) - access while replacing
// ============================================================
testHeader('A2×B1: Access while replacing (small/blockdb)');

$originalContent = generateKnownContent(SMALL_SIZE);
$replacementContent = generateKnownContent(SMALL_SIZE + 50); // Different size

// PUT original
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect: $errstr");
$request = "PUT /t6_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($originalContent) . "\r\n\r\n$originalContent";
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

// Verify original exists
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t6_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $originalContent, 'GET original');
testResult('GET original returns correct content', $passed);
$allPassed = $allPassed && $passed;

// Start async replacement PUT with pause
$info = startAsyncPutWithPause($host, $port, 't6_small', 'k1', $replacementContent);

// GET during replacement - should get 404 (old soft-deleted, new writing)
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t6_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('GET during replacement returns 404', $passed);
$allPassed = $allPassed && $passed;

// Complete replacement
signalAsyncPutContinue($info);
$putResponse = waitAsyncPut($info);
$passed = strpos($putResponse, '200 OK') !== false;
testResult('Replacement PUT completes', $passed);
$allPassed = $allPassed && $passed;

// GET after replacement - should get new content
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t6_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $replacementContent, 'GET after replacement');
testResult('GET after replacement returns new content', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// A2 × B2: Large content (file) - access while replacing
// ============================================================
testHeader('A2×B2: Access while replacing (large/file)');

$originalContent = generateKnownContent(LARGE_SIZE);
$replacementContent = generateKnownContent(LARGE_SIZE + 100);

// PUT original
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect: $errstr");
$request = "PUT /t6_large/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($originalContent) . "\r\n\r\n$originalContent";
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

// Start async replacement
$info = startAsyncPutWithPause($host, $port, 't6_large', 'k1', $replacementContent);

// GET during replacement - should get 404
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t6_large/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('GET during large replacement returns 404', $passed);
$allPassed = $allPassed && $passed;

// Complete replacement
signalAsyncPutContinue($info);
$putResponse = waitAsyncPut($info);
$passed = strpos($putResponse, '200 OK') !== false;
testResult('Large replacement PUT completes', $passed);
$allPassed = $allPassed && $passed;

// GET after replacement
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t6_large/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $replacementContent, 'GET after large replacement');
testResult('GET after large replacement returns new content', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Variant: Size switch during replacement (small→large)
// ============================================================
testHeader('A2 Variant: Size switch small→large during replacement');

$smallContent = generateKnownContent(SMALL_SIZE);
$largeContent = generateKnownContent(LARGE_SIZE);

// PUT small
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t6_switch/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($smallContent) . "\r\n\r\n$smallContent";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

// Replace with large (block→file switch)
$info = startAsyncPutWithPause($host, $port, 't6_switch', 'k1', $largeContent);

// GET during switch
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t6_switch/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('GET during small→large switch returns 404', $passed);
$allPassed = $allPassed && $passed;

signalAsyncPutContinue($info);
waitAsyncPut($info);

// Verify large content
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t6_switch/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $largeContent, 'GET after small→large switch');
testResult('GET after small→large switch returns correct content', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Variant: Size switch during replacement (large→small)
// ============================================================
testHeader('A2 Variant: Size switch large→small during replacement');

$largeContent2 = generateKnownContent(LARGE_SIZE);
$smallContent2 = generateKnownContent(SMALL_SIZE);

// PUT large
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t6_switch2/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($largeContent2) . "\r\n\r\n$largeContent2";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 5);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

// Replace with small (file→block switch)
$info = startAsyncPutWithPause($host, $port, 't6_switch2', 'k1', $smallContent2);

// GET during switch
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t6_switch2/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('GET during large→small switch returns 404', $passed);
$allPassed = $allPassed && $passed;

signalAsyncPutContinue($info);
waitAsyncPut($info);

// Verify small content
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t6_switch2/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $smallContent2, 'GET after large→small switch');
testResult('GET after large→small switch returns correct content', $passed);
$allPassed = $allPassed && $passed;

echo "\n";
if ($allPassed) {
    echo "test_6_access_while_replacing: ALL TESTS PASSED\n";
    exit(0);
} else {
    echo "test_6_access_while_replacing: SOME TESTS FAILED\n";
    exit(1);
}