<?php
/**
 * test_10_content_expired_while_reading.php
 * Matrix A6: Content expired while reading
 *   B1: Small update (blockdb)
 *   B2: Large update (file)
 *
 * Verifies: Keys with TTL expire correctly. GET before expiry succeeds,
 * GET after expiry returns 404. Multiple keys with staggered TTLs.
 */

require_once __DIR__ . '/DataConsistencyHelper.php';

$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);

$allPassed = true;

// ============================================================
// A6 × B1: Small content (blockdb) - expired while reading
// ============================================================
testHeader('A6×B1: Content expired (small/blockdb)');

$content = generateKnownContent(SMALL_SIZE);

// PUT with 1-second TTL
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect: $errstr");
$request = "PUT /t10_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nX-Ttl: 1\r\nContent-Length: " . strlen($content) . "\r\n\r\n$content";
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
testResult('PUT with TTL succeeds', $passed);
$allPassed = $allPassed && $passed;

// GET immediately - should succeed
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t10_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $content, 'GET before expiry');
testResult('GET before expiry returns correct content', $passed);
$allPassed = $allPassed && $passed;

// Wait for expiry (2 seconds to be safe)
echo "  Waiting for TTL expiry...\n";
sleep(2);

// GET after expiry - should return 404
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t10_small/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('GET after expiry returns 404', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// A6 × B2: Large content (file) - expired
// ============================================================
testHeader('A6×B2: Content expired (large/file)');

$content = generateKnownContent(LARGE_SIZE);

// PUT with 1-second TTL
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t10_large/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nX-Ttl: 1\r\nContent-Length: " . strlen($content) . "\r\n\r\n$content";
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
testResult('PUT large with TTL succeeds', $passed);
$allPassed = $allPassed && $passed;

// GET immediately
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t10_large/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $content, 'GET large before expiry');
testResult('GET large before expiry returns correct content', $passed);
$allPassed = $allPassed && $passed;

// Wait for expiry
echo "  Waiting for TTL expiry...\n";
sleep(2);

// GET after expiry
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t10_large/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('GET large after expiry returns 404', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Variant: Staggered TTLs on multiple keys
// ============================================================
testHeader('A6 Variant: Staggered TTLs');

$content1 = generateKnownContent(50);
$content2 = generateKnownContent(60);
$content3 = generateKnownContent(70);

// PUT three keys with different TTLs: 1s, 3s, 5s
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t10_stagger/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nX-Ttl: 1\r\nContent-Length: 50\r\n\r\n$content1";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t10_stagger/k2 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nX-Ttl: 3\r\nContent-Length: 60\r\n\r\n$content2";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t10_stagger/k3 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nX-Ttl: 5\r\nContent-Length: 70\r\n\r\n$content3";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

// All three should exist now
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t10_stagger/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('k1 exists before expiry', $passed);
$allPassed = $allPassed && $passed;

// Wait 2 seconds - k1 should expire, k2 and k3 should still exist
echo "  Waiting 2 seconds...\n";
sleep(2);

$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t10_stagger/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('k1 expired after 2s', $passed);
$allPassed = $allPassed && $passed;

$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t10_stagger/k2 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('k2 still exists after 2s', $passed);
$allPassed = $allPassed && $passed;

$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t10_stagger/k3 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('k3 still exists after 2s', $passed);
$allPassed = $allPassed && $passed;

// Wait 2 more seconds - k2 should expire, k3 should still exist
echo "  Waiting 2 more seconds...\n";
sleep(2);

$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t10_stagger/k2 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('k2 expired after 4s', $passed);
$allPassed = $allPassed && $passed;

$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t10_stagger/k3 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('k3 still exists after 4s', $passed);
$allPassed = $allPassed && $passed;

echo "\n";
if ($allPassed) {
    echo "test_10_content_expired_while_reading: ALL TESTS PASSED\n";
    exit(0);
} else {
    echo "test_10_content_expired_while_reading: SOME TESTS FAILED\n";
    exit(1);
}