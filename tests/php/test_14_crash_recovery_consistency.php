<?php
/**
 * test_14_crash_recovery_consistency.php
 * Crash recovery verification via SIGKILL.
 *
 * Tests:
 * - PUT several keys (mix small/large), kill server, restart, verify all exist
 * - Async PUT with pause, kill server, restart, verify key not present
 * - PUT → DELETE → kill → restart → verify key gone
 * - PUT → replace (complete) → kill → restart → verify new value
 *
 * NOTE: This test requires the server to be started with --pidfile and
 * the test must be able to restart it. The test runner script handles this.
 */

require_once __DIR__ . '/DataConsistencyHelper.php';

$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);

// Paths must match what the test runner uses
$pidFile = getenv('SCACHE_PIDFILE') ?: '/tmp/scache-consistency-test.pid';
$dbDir = getenv('SCACHE_DBDIR') ?: '/tmp/scache-consistency-test-db';
$scacheBin = getenv('SCACHE_BIN') ?: __DIR__ . '/../../src/server/scache';

$allPassed = true;

/**
 * Restart the server and wait for it to be ready.
 */
function restartServer($host, $port, $pidFile, $dbDir, $scacheBin) {
    // Kill existing server
    killServer($pidFile);
    
    // Start new server with correct CLI flags
    $cmd = sprintf(
        '%s -r %s -b %s:%d -m %s -d &',
        escapeshellcmd($scacheBin),
        escapeshellarg($dbDir),
        escapeshellarg($host),
        $port,
        escapeshellarg($pidFile)
    );
    
    exec($cmd);
    
    // Wait for server to be ready
    for ($i = 0; $i < 60; $i++) {
        if (file_exists($pidFile)) {
            $pid = (int)trim(file_get_contents($pidFile));
            if ($pid > 0 && posix_kill($pid, 0)) {
                usleep(500000);
                // Verify it's listening
                $sock = @fsockopen($host, $port, $errno, $errstr, 2);
                if ($sock) {
                    fclose($sock);
                    return true;
                }
            }
        }
        usleep(500000);
    }
    echo "FAILED: Could not restart server\n";
    return false;
}

// ============================================================
// PUT several keys, kill, restart, verify all exist
// ============================================================
testHeader('Crash recovery: PUT keys, kill, restart, verify');

$keys = [
    'k_small1' => generateKnownContent(100),
    'k_small2' => generateKnownContent(200),
    'k_large1' => generateKnownContent(5000),
    'k_small3' => generateKnownContent(300),
    'k_large2' => generateKnownContent(6000),
];

foreach ($keys as $key => $content) {
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    assertOrDie($sock !== false, "Could not connect: $errstr");
    $request = "PUT /t14_persist/$key HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($content) . "\r\n\r\n$content";
    fwrite($sock, $request);
    $response = '';
    stream_set_timeout($sock, 5);
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

// Wait a moment for any pending flush
sleep(1);

// Kill server hard
echo "  Killing server with SIGKILL...\n";
killServer($pidFile, SIGKILL);

// Restart
echo "  Restarting server...\n";
$restarted = restartServer($host, $port, $pidFile, $dbDir, $scacheBin);
assertOrDie($restarted, "Server restart failed");

// Verify all keys survived
foreach ($keys as $key => $expectedContent) {
    $sock = @fsockopen($host, $port, $errno, $errstr, 5);
    assertOrDie($sock !== false, "Could not connect: $errstr");
    $request = "GET /t14_persist/$key HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
    $passed = verifyContent($body, $expectedContent, "GET $key after restart");
    testResult("Key $key survives restart", $passed);
    $allPassed = $allPassed && $passed;
}

// ============================================================
// Async PUT with pause, kill, restart, verify key not present
// ============================================================
testHeader('Crash recovery: Interrupted write not persisted');

$content = generateKnownContent(SMALL_SIZE);

// Start async PUT with pause
$info = startAsyncPutWithPause($host, $port, 't14_interrupt', 'k1', $content);

// Kill server while PUT is paused (writing=true, not in index)
echo "  Killing server during paused PUT...\n";
killServer($pidFile, SIGKILL);

// Clean up the paused child
signalAsyncPutKill($info);
waitAsyncPut($info);

// Restart
echo "  Restarting server...\n";
$restarted = restartServer($host, $port, $pidFile, $dbDir, $scacheBin);
assertOrDie($restarted, "Server restart failed");

// GET - should return 404 (partial write not persisted)
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t14_interrupt/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('Interrupted write not persisted after restart', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// PUT → DELETE → kill → restart → verify key gone
// ============================================================
testHeader('Crash recovery: Deleted key stays deleted');

$content = generateKnownContent(SMALL_SIZE);

// PUT
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t14_delete/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($content) . "\r\n\r\n$content";
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

// DELETE
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "DELETE /t14_delete/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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

// Wait for flush
sleep(1);

// Kill and restart
echo "  Killing server...\n";
killServer($pidFile, SIGKILL);
echo "  Restarting server...\n";
$restarted = restartServer($host, $port, $pidFile, $dbDir, $scacheBin);
assertOrDie($restarted, "Server restart failed");

// GET - should return 404
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t14_delete/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
testResult('Deleted key stays deleted after restart', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// PUT → replace (complete) → kill → restart → verify new value
// ============================================================
testHeader('Crash recovery: Replaced value persists');

$originalContent = generateKnownContent(SMALL_SIZE);
$replacementContent = generateKnownContent(SMALL_SIZE + 100);

// PUT original
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t14_replace/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($originalContent) . "\r\n\r\n$originalContent";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 3);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

// PUT replacement (complete)
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t14_replace/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($replacementContent) . "\r\n\r\n$replacementContent";
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
testResult('Replace PUT succeeds', $passed);
$allPassed = $allPassed && $passed;

// Wait for flush
sleep(1);

// Kill and restart
echo "  Killing server...\n";
killServer($pidFile, SIGKILL);
echo "  Restarting server...\n";
$restarted = restartServer($host, $port, $pidFile, $dbDir, $scacheBin);
assertOrDie($restarted, "Server restart failed");

// GET - should return replacement content
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "GET /t14_replace/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
$passed = verifyContent($body, $replacementContent, 'GET after restart');
testResult('Replaced value persists after restart', $passed);
$allPassed = $allPassed && $passed;

echo "\n";
if ($allPassed) {
    echo "test_14_crash_recovery_consistency: ALL TESTS PASSED\n";
    exit(0);
} else {
    echo "test_14_crash_recovery_consistency: SOME TESTS FAILED\n";
    exit(1);
}