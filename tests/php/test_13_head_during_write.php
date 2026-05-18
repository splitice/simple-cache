<?php
/**
 * test_13_head_during_write.php
 * HEAD request consistency during writes.
 *
 * Tests:
 * - HEAD during initial PUT → 404
 * - HEAD after PUT completes → 200 with correct Content-Length
 * - HEAD during replacement → 404
 */

require_once __DIR__ . '/DataConsistencyHelper.php';

$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);

$allPassed = true;

// ============================================================
// HEAD during initial PUT
// ============================================================
testHeader('HEAD during initial PUT');

$content = generateKnownContent(SMALL_SIZE);

// Start async PUT with pause
$info = startAsyncPutWithPause($host, $port, 't13_write', 'k1', $content);

// HEAD during write - should get 404
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect: $errstr");
$request = "HEAD /t13_write/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('HEAD during write returns 404', $passed);
$allPassed = $allPassed && $passed;

// Complete PUT
signalAsyncPutContinue($info);
$putResponse = waitAsyncPut($info);
$passed = strpos($putResponse, '200 OK') !== false;
testResult('PUT completes', $passed);
$allPassed = $allPassed && $passed;

// HEAD after completion - should get 200 with correct Content-Length
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "HEAD /t13_write/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('HEAD after write returns 200', $passed);
$allPassed = $allPassed && $passed;

$expectedCL = 'Content-Length: ' . strlen($content);
$passed = strpos($response, $expectedCL) !== false;
testResult('HEAD Content-Length is correct', $passed);
$allPassed = $allPassed && $passed;

// HEAD should not have a body (just headers + \r\n\r\n)
$bodyStart = strpos($response, "\r\n\r\n");
$bodyAfter = $bodyStart !== false ? trim(substr($response, $bodyStart + 4)) : '';
$passed = $bodyAfter === '';
testResult('HEAD response has no body', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// HEAD during replacement
// ============================================================
testHeader('HEAD during replacement');

$originalContent = generateKnownContent(SMALL_SIZE);
$replacementContent = generateKnownContent(SMALL_SIZE + 50);

// PUT original
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t13_replace/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($originalContent) . "\r\n\r\n$originalContent";
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
testResult('PUT original succeeds', $passed);
$allPassed = $allPassed && $passed;

// HEAD before replacement - should succeed
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "HEAD /t13_replace/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('HEAD before replacement returns 200', $passed);
$allPassed = $allPassed && $passed;

// Start async replacement
$info = startAsyncPutWithPause($host, $port, 't13_replace', 'k1', $replacementContent);

// HEAD during replacement - should get 404
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "HEAD /t13_replace/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('HEAD during replacement returns 404', $passed);
$allPassed = $allPassed && $passed;

// Complete replacement
signalAsyncPutContinue($info);
waitAsyncPut($info);

// HEAD after replacement
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "HEAD /t13_replace/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('HEAD after replacement returns 200', $passed);
$allPassed = $allPassed && $passed;

$expectedCL = 'Content-Length: ' . strlen($replacementContent);
$passed = strpos($response, $expectedCL) !== false;
testResult('HEAD after replacement has correct Content-Length', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// HEAD on non-existing key
// ============================================================
testHeader('HEAD on non-existing key');

$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "HEAD /t13_nonexist/nokey HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('HEAD on non-existing key returns 404', $passed);
$allPassed = $allPassed && $passed;

echo "\n";
if ($allPassed) {
    echo "test_13_head_during_write: ALL TESTS PASSED\n";
    exit(0);
} else {
    echo "test_13_head_during_write: SOME TESTS FAILED\n";
    exit(1);
}