<?php
/**
 * test_9_content_deleted_while_reading.php
 * Matrix A5: Content deleted while reading
 *   B1: Small update (blockdb)
 *   B2: Large update (file)
 *
 * Verifies: When a GET is in progress and the key is deleted, the GET
 * still completes with the full correct content (refcount protection).
 * Also tests table delete while reading a key within it.
 */

require_once __DIR__ . '/DataConsistencyHelper.php';

$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);

$allPassed = true;

// ============================================================
// A5 × B1: Small content (blockdb) - delete while reading
// ============================================================
testHeader('A5×B1: Delete while reading (small/blockdb)');

$content = generateKnownContent(SMALL_SIZE);

// PUT content
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect: $errstr");
$request = "PUT /t9_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($content) . "\r\n\r\n$content";
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
testResult('PUT succeeds', $passed);
$allPassed = $allPassed && $passed;

// Start async GET (slow consumer)
$getInfo = startAsyncGet($host, $port, 't9_small', 'k1');

// While GET is in progress, DELETE the key
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect for DELETE during GET: $errstr");
$request = "DELETE /t9_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 1);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);
$passed = strpos($response, '200 OK') !== false || strpos($response, 'DELETED') !== false;
testResult('DELETE succeeds while GET in progress', $passed);
$allPassed = $allPassed && $passed;

// Wait for GET to complete - should still get full content (refcount protection)
$getResult = waitAsyncGet($getInfo);
$passed = verifyContent($getResult, $content, 'GET result during delete');
testResult('GET completes with full content despite delete', $passed);
$allPassed = $allPassed && $passed;

// Subsequent GET should return 404
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect for subsequent GET: $errstr");
$request = "GET /t9_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('Subsequent GET returns 404', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// A5 × B2: Large content (file) - delete while reading
// ============================================================
testHeader('A5×B2: Delete while reading (large/file)');

$content = generateKnownContent(LARGE_SIZE);

// PUT large content
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect for large PUT: $errstr");
$request = "PUT /t9_large/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($content) . "\r\n\r\n$content";
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
testResult('PUT large content succeeds', $passed);
$allPassed = $allPassed && $passed;

// Start async GET
$getInfo = startAsyncGet($host, $port, 't9_large', 'k1');

// DELETE while GET in progress
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect for large DELETE during GET: $errstr");
$request = "DELETE /t9_large/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 1);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

// Wait for GET
$getResult = waitAsyncGet($getInfo);
$passed = verifyContent($getResult, $content, 'GET large result during delete');
testResult('GET large completes with full content despite delete', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Variant: Table delete while reading a key within it
// ============================================================
testHeader('A5 Variant: Table delete while reading');

$content = generateKnownContent(SMALL_SIZE);

// PUT key in table
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect for table PUT: $errstr");
$request = "PUT /t9_table/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($content) . "\r\n\r\n$content";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 1);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

// Start async GET
$getInfo = startAsyncGet($host, $port, 't9_table', 'k1');

// DELETE entire table while GET in progress
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect for table DELETE during GET: $errstr");
$request = "DELETE /t9_table HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 1);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);
$passed = strpos($response, '200 OK') !== false || strpos($response, 'DELETED') !== false;
testResult('Table DELETE succeeds while GET in progress', $passed);
$allPassed = $allPassed && $passed;

// GET should still complete with full content
$getResult = waitAsyncGet($getInfo);
$passed = verifyContent($getResult, $content, 'GET result during table delete');
testResult('GET completes with full content despite table delete', $passed);
$allPassed = $allPassed && $passed;

echo "\n";
if ($allPassed) {
    echo "test_9_content_deleted_while_reading: ALL TESTS PASSED\n";
    exit(0);
} else {
    echo "test_9_content_deleted_while_reading: SOME TESTS FAILED\n";
    exit(1);
}
