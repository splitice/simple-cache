<?php
/**
 * test_16_concurrent_readers.php
 * Multiple concurrent GETs on same key.
 *
 * Tests:
 * - 10 children GET same key simultaneously → all get correct content
 * - Mixed GET/PUT/GET on same key → all GETs return consistent data
 */

require_once __DIR__ . '/DataConsistencyHelper.php';

$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);

$allPassed = true;

function isExpectedVersion($actual, $expected)
{
    return is_string($actual) && $actual === $expected;
}

// ============================================================
// 10 concurrent GETs on same key
// ============================================================
testHeader('Concurrent readers: 10 simultaneous GETs');

$content = generateKnownContent(SMALL_SIZE);

// PUT the key
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
assertOrDie($sock !== false, "Could not connect: $errstr");
$request = "PUT /t16_concurrent/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($content) . "\r\n\r\n$content";
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

// Fork 10 children, all GET same key
$children = [];
$numChildren = 10;

for ($i = 0; $i < $numChildren; $i++) {
    $resultFile = tempnam(sys_get_temp_dir(), 'scache_cr_');
    
    $pid = pcntl_fork();
    if ($pid == -1) {
        echo "FAILED: Could not fork\n";
        exit(1);
    }
    
    if ($pid == 0) {
        // Child: do GET
        $sock = @fsockopen($host, $port, $errno, $errstr, 5);
        if (!$sock) {
            file_put_contents($resultFile, "error:socket");
            exit(1);
        }
        
        $request = "GET /t16_concurrent/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
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
        file_put_contents($resultFile, $body);
        exit(0);
    }
    
    $children[] = ['pid' => $pid, 'file' => $resultFile];
}

// Wait for all children
$allCorrect = true;
foreach ($children as $child) {
    pcntl_waitpid($child['pid'], $status);
    $result = @file_get_contents($child['file']);
    @unlink($child['file']);
    
    if (!verifyContent($result, $content, 'concurrent GET')) {
        $allCorrect = false;
    }
}

$passed = $allCorrect;
testResult('All 10 concurrent GETs return correct content', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Mixed GET/PUT/GET on same key
// ============================================================
testHeader('Concurrent readers: Mixed GET/PUT/GET');

$originalContent = generateKnownContent(SMALL_SIZE);
$newContent = generateKnownContent(SMALL_SIZE + 50);

// PUT original
$sock = @fsockopen($host, $port, $errno, $errstr, 5);
$request = "PUT /t16_mixed/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($originalContent) . "\r\n\r\n$originalContent";
fwrite($sock, $request);
$response = '';
stream_set_timeout($sock, 1);
while (!feof($sock)) {
    $data = @fread($sock, 4096);
    if ($data === false || $data === '') break;
    $response .= $data;
}
fclose($sock);

// Fork children doing mixed operations
$children = [];
$numChildren = 10;

for ($i = 0; $i < $numChildren; $i++) {
    $resultFile = tempnam(sys_get_temp_dir(), 'scache_mix_');
    
    $pid = pcntl_fork();
    if ($pid == -1) {
        echo "FAILED: Could not fork\n";
        exit(1);
    }
    
    if ($pid == 0) {
        $sock = @fsockopen($host, $port, $errno, $errstr, 5);
        if (!$sock) {
            file_put_contents($resultFile, "error:socket");
            exit(1);
        }
        
        // Each child does: GET, then maybe PUT, then GET
        $results = [];
        
        // GET 1
        $request = "GET /t16_mixed/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
        fwrite($sock, $request);
        $response = '';
        stream_set_timeout($sock, 1);
        while (!feof($sock)) {
            $data = @fread($sock, 4096);
            if ($data === false || $data === '') break;
            $response .= $data;
        }
        $bodyStart = strpos($response, "\r\n\r\n");
        $results['get1'] = $bodyStart !== false ? substr($response, $bodyStart + 4) : null;
        
        // Some children do PUT
        if ($i % 3 == 0) {
            $request = "PUT /t16_mixed/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\nContent-Length: " . strlen($newContent) . "\r\n\r\n$newContent";
            fwrite($sock, $request);
            $response = '';
            stream_set_timeout($sock, 1);
            while (!feof($sock)) {
                $data = @fread($sock, 4096);
                if ($data === false || $data === '') break;
                $response .= $data;
            }
            $results['put'] = strpos($response, '200 OK') !== false;
        }
        
        // GET 2
        $request = "GET /t16_mixed/k1 HTTP/1.1\r\nHost: $host:$port\r\nConnection: Keep-Alive\r\n\r\n";
        fwrite($sock, $request);
        $response = '';
        stream_set_timeout($sock, 1);
        while (!feof($sock)) {
            $data = @fread($sock, 4096);
            if ($data === false || $data === '') break;
            $response .= $data;
        }
        $bodyStart = strpos($response, "\r\n\r\n");
        $results['get2'] = $bodyStart !== false ? substr($response, $bodyStart + 4) : null;
        
        fclose($sock);
        file_put_contents($resultFile, serialize($results));
        exit(0);
    }
    
    $children[] = ['pid' => $pid, 'file' => $resultFile];
}

// Wait for all children and verify consistency
$allConsistent = true;
foreach ($children as $child) {
    pcntl_waitpid($child['pid'], $status);
    $data = @file_get_contents($child['file']);
    @unlink($child['file']);
    
    if ($data === false || strpos($data, 'error:') === 0) {
        $allConsistent = false;
        continue;
    }
    
    $results = @unserialize($data);
    if (!is_array($results)) {
        $allConsistent = false;
        continue;
    }
    
    // Each GET should return either original or new content (never partial/corrupt)
    foreach (['get1', 'get2'] as $getKey) {
        if (isset($results[$getKey]) && $results[$getKey] !== null) {
            $isOriginal = isExpectedVersion($results[$getKey], $originalContent);
            $isNew = isExpectedVersion($results[$getKey], $newContent);
            if (!$isOriginal && !$isNew) {
                verifyContent($results[$getKey], $originalContent, $getKey);
                echo "  FAILED: GET returned neither original nor new content\n";
                $allConsistent = false;
            }
        }
    }
}

$passed = $allConsistent;
testResult('Mixed GET/PUT/GET returns consistent data (old or new, never corrupt)', $passed);
$allPassed = $allPassed && $passed;

echo "\n";
if ($allPassed) {
    echo "test_16_concurrent_readers: ALL TESTS PASSED\n";
    exit(0);
} else {
    echo "test_16_concurrent_readers: SOME TESTS FAILED\n";
    exit(1);
}
