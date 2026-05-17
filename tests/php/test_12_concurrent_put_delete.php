<?php
/**
 * test_12_concurrent_put_delete.php
 * Exercise PUT+DELETE race on same key.
 *
 * Tests:
 * - Async PUT with pause, then DELETE same key, then complete PUT
 * - DELETE then immediately PUT same key
 *
 * Verifies consistency: no crashes, no corrupt data.
 */

require_once __DIR__ . '/DataConsistencyHelper.php';

$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);

$allPassed = true;

// ============================================================
// Async PUT with pause → DELETE → complete PUT
// ============================================================
testHeader('Concurrent: PUT (paused) + DELETE + complete PUT');

$content = generateKnownContent(SMALL_SIZE);

// Start async PUT with pause
$info = startAsyncPutWithPause($host, $port, 't12_race', 'k1', $content);

// While PUT is paused, DELETE the same key
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect: $errstr");
$request = "DELETE /t12_race/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

// DELETE may return 404 (entry is writing, not yet in hash) or 200
$deleteOk = strpos($response, '200') !== false || strpos($response, '404') !== false;
$passed = $deleteOk;
testResult('DELETE during paused PUT does not crash', $passed);
$allPassed = $allPassed && $passed;

// Now complete the PUT
signalAsyncPutContinue($info);
$putResponse = waitAsyncPut($info);

// The PUT should complete (either 200 or the entry was already deleted)
$putOk = $putResponse !== null;
$passed = $putOk;
testResult('PUT completes after concurrent DELETE', $passed);
$allPassed = $allPassed && $passed;

// GET - verify consistent state (either key exists with correct content, or 404)
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t12_race/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$is200 = strpos($response, '200 OK') !== false;
$passed = $is404 || $is200;
if ($is200) {
    $bodyStart = strpos($response, "\r\n\r\n");
    $body = $bodyStart !== false ? substr($response, $bodyStart + 4) : '';
    $passed = verifyContent($body, $content, 'GET after race');
}
testResult('GET after PUT+DELETE race returns consistent state', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// DELETE then immediately PUT same key
// ============================================================
testHeader('Concurrent: DELETE then PUT same key');

// First PUT a key
$originalContent = generateKnownContent(SMALL_SIZE);
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t12_race2/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($originalContent) . "\r\n\r\n$originalContent";
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

// DELETE the key
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "DELETE /t12_race2/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('DELETE succeeds', $passed);
$allPassed = $allPassed && $passed;

// Immediately PUT new value
$newContent = generateKnownContent(SMALL_SIZE + 20);
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t12_race2/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($newContent) . "\r\n\r\n$newContent";
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
testResult('PUT after DELETE succeeds', $passed);
$allPassed = $allPassed && $passed;

// Verify new value
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t12_race2/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $newContent, 'GET after DELETE+PUT');
testResult('GET after DELETE+PUT returns new content', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Rapid PUT/DELETE/PUT cycle (stress test)
// ============================================================
testHeader('Concurrent: Rapid PUT/DELETE/PUT cycle');

for ($i = 0; $i < 10; $i++) {
    $content = generateKnownContent(50);
    
    // PUT
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    $request = "PUT /t12_cycle/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: 50\r\n\r\n$content";
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
        testResult("Cycle $i: PUT failed", false);
        $allPassed = false;
        break;
    }
    
    // DELETE
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    $request = "DELETE /t12_cycle/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
    fwrite($sock, $request);
    $response = '';
    stream_set_timeout($sock, 3);
    while (!feof($sock)) {
        $data = @fread($sock, 4096);
        if ($data === false || $data === '') break;
        $response .= $data;
    }
    fclose($sock);
    
    if (strpos($response, '200') === false && strpos($response, 'DELETED') === false) {
        testResult("Cycle $i: DELETE failed", false);
        $allPassed = false;
        break;
    }
}

if ($allPassed) {
    testResult('Rapid PUT/DELETE cycle (10 iterations)', true);
}

echo "\n";
if ($allPassed) {
    echo "test_12_concurrent_put_delete: ALL TESTS PASSED\n";
    exit(0);
} else {
    echo "test_12_concurrent_put_delete: SOME TESTS FAILED\n";
    exit(1);
}