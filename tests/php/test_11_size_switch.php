<?php
/**
 * test_11_size_switch.php
 * Exercise db_target_write_allocate block↔file transitions.
 *
 * Tests:
 * - Small→Large (block→file switch, old block freed)
 * - Large→Small (file→block switch, old file unlinked)
 * - Boundary: exactly 4096B (block) and 4097B (file)
 */

require_once __DIR__ . '/DataConsistencyHelper.php';

$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);

$allPassed = true;

// ============================================================
// Small → Large (block→file switch)
// ============================================================
testHeader('Size switch: Small → Large (block→file)');

$smallContent = generateKnownContent(SMALL_SIZE);
$largeContent = generateKnownContent(LARGE_SIZE);

// PUT small
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect: $errstr");
$request = "PUT /t11_s2l/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($smallContent) . "\r\n\r\n$smallContent";
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
testResult('PUT small succeeds', $passed);
$allPassed = $allPassed && $passed;

// Verify small
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t11_s2l/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $smallContent, 'GET small');
testResult('GET small returns correct content', $passed);
$allPassed = $allPassed && $passed;

// PUT large (replaces small, triggers block→file switch)
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t11_s2l/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($largeContent) . "\r\n\r\n$largeContent";
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
testResult('PUT large (block→file) succeeds', $passed);
$allPassed = $allPassed && $passed;

// Verify large
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t11_s2l/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $largeContent, 'GET after block→file');
testResult('GET after block→file switch returns correct content', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Large → Small (file→block switch)
// ============================================================
testHeader('Size switch: Large → Small (file→block)');

$largeContent2 = generateKnownContent(LARGE_SIZE);
$smallContent2 = generateKnownContent(SMALL_SIZE);

// PUT large
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t11_l2s/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($largeContent2) . "\r\n\r\n$largeContent2";
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
testResult('PUT large succeeds', $passed);
$allPassed = $allPassed && $passed;

// PUT small (replaces large, triggers file→block switch)
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t11_l2s/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($smallContent2) . "\r\n\r\n$smallContent2";
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
testResult('PUT small (file→block) succeeds', $passed);
$allPassed = $allPassed && $passed;

// Verify small
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t11_l2s/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $smallContent2, 'GET after file→block');
testResult('GET after file→block switch returns correct content', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Boundary: exactly 4096 bytes (block)
// ============================================================
testHeader('Boundary: exactly 4096 bytes (block)');

$content4096 = generateKnownContent(BLOCK_SIZE);

$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t11_boundary/k4096 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . BLOCK_SIZE . "\r\n\r\n$content4096";
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
testResult('PUT 4096B succeeds', $passed);
$allPassed = $allPassed && $passed;

$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t11_boundary/k4096 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $content4096, 'GET 4096B');
testResult('GET 4096B returns correct content', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Boundary: exactly 4097 bytes (file)
// ============================================================
testHeader('Boundary: exactly 4097 bytes (file)');

$content4097 = generateKnownContent(BLOCK_SIZE + 1);

$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t11_boundary/k4097 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . (BLOCK_SIZE + 1) . "\r\n\r\n$content4097";
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
testResult('PUT 4097B succeeds', $passed);
$allPassed = $allPassed && $passed;

$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t11_boundary/k4097 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $content4097, 'GET 4097B');
testResult('GET 4097B returns correct content', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Multiple size switches on same key
// ============================================================
testHeader('Multiple size switches on same key');

$sizes = [100, 5000, 200, 6000, 300, 4096, 4097, 100];
$key = 'k_multi';

foreach ($sizes as $idx => $size) {
    $content = generateKnownContent($size);
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    $request = "PUT /t11_multi/$key HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: $size\r\n\r\n$content";
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
    if (!$passed) {
        testResult("PUT size $size (switch #$idx)", false);
        $allPassed = false;
        continue;
    }
    
    // Verify
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    $request = "GET /t11_multi/$key HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
    $passed = verifyContent($body, $content, "GET after switch #$idx (size $size)");
    testResult("Switch #$idx: size $size", $passed);
    $allPassed = $allPassed && $passed;
}

echo "\n";
if ($allPassed) {
    echo "test_11_size_switch: ALL TESTS PASSED\n";
    exit(0);
} else {
    echo "test_11_size_switch: SOME TESTS FAILED\n";
    exit(1);
}