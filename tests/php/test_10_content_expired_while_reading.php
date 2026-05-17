<?php
/**
 * test_10_content_expired_while_reading.php
 * Matrix A6: Content expired while reading
 *   B1: Small update (blockdb)
 *   B2: Large update (file)
 *
 * Verifies: Keys with TTL expire correctly. GET before expiry succeeds,
 * GET after expiry returns 404. Multiple keys with staggered TTLs.
 * Uses ApiClient for proper X-TTL header handling.
 */

use Splitice\SimpleCache\ApiClient;

require_once __DIR__ . '/vendor/autoload.php';
require_once __DIR__ . '/DataConsistencyHelper.php';

$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);
$baseUrl = "http://$host:$port";

$allPassed = true;

// ============================================================
// A6 × B1: Small content (blockdb) - expired while reading
// ============================================================
testHeader('A6×B1: Content expired (small/blockdb)');

$ac = new ApiClient($baseUrl);
$content = generateKnownContent(SMALL_SIZE);

// PUT with 2-second TTL using ApiClient (proper X-TTL handling)
try {
    $ac->key_put('t10_small', 'k1', $content, 2);
    $passed = true;
} catch (Exception $e) {
    $passed = false;
    echo "PUT failed: " . $e->getMessage() . "\n";
}
testResult('PUT with TTL succeeds', $passed);
$allPassed = $allPassed && $passed;

// GET immediately - should succeed
$result = $ac->key_get('t10_small', 'k1');
$passed = verifyContent($result, $content, 'GET before expiry');
testResult('GET before expiry returns correct content', $passed);
$allPassed = $allPassed && $passed;

// Wait for expiry (3 seconds to be safe, timer may have 1s granularity)
echo "  Waiting for TTL expiry...\n";
sleep(3);

// GET after expiry - should return null (404)
$result = $ac->key_get('t10_small', 'k1');
$passed = $result === null;
testResult('GET after expiry returns 404', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// A6 × B2: Large content (file) - expired
// ============================================================
testHeader('A6×B2: Content expired (large/file)');

$ac2 = new ApiClient($baseUrl);
$content = generateKnownContent(LARGE_SIZE);

try {
    $ac2->key_put('t10_large', 'k1', $content, 2);
    $passed = true;
} catch (Exception $e) {
    $passed = false;
    echo "PUT failed: " . $e->getMessage() . "\n";
}
testResult('PUT large with TTL succeeds', $passed);
$allPassed = $allPassed && $passed;

// GET immediately
$result = $ac2->key_get('t10_large', 'k1');
$passed = verifyContent($result, $content, 'GET large before expiry');
testResult('GET large before expiry returns correct content', $passed);
$allPassed = $allPassed && $passed;

// Wait for expiry
echo "  Waiting for TTL expiry...\n";
sleep(3);

// GET after expiry
$result = $ac2->key_get('t10_large', 'k1');
$passed = $result === null;
testResult('GET large after expiry returns 404', $passed);
$allPassed = $allPassed && $passed;

// ============================================================
// Variant: Staggered TTLs on multiple keys
// ============================================================
testHeader('A6 Variant: Staggered TTLs');

$ac3 = new ApiClient($baseUrl);
$content1 = generateKnownContent(50);
$content2 = generateKnownContent(60);
$content3 = generateKnownContent(70);

// PUT three keys with different TTLs: 2s, 5s, 8s
try {
    $ac3->key_put('t10_stagger', 'k1', $content1, 2);
    $ac3->key_put('t10_stagger', 'k2', $content2, 5);
    $ac3->key_put('t10_stagger', 'k3', $content3, 8);
    $passed = true;
} catch (Exception $e) {
    $passed = false;
    echo "PUT failed: " . $e->getMessage() . "\n";
}
testResult('PUT staggered TTLs succeeds', $passed);
$allPassed = $allPassed && $passed;

// All three should exist now
$result = $ac3->key_get('t10_stagger', 'k1');
$passed = $result !== null && verifyContent($result, $content1);
testResult('k1 exists before expiry', $passed);
$allPassed = $allPassed && $passed;

// Wait 3 seconds - k1 should expire, k2 and k3 should still exist
echo "  Waiting 3 seconds...\n";
sleep(3);

$result = $ac3->key_get('t10_stagger', 'k1');
$passed = $result === null;
testResult('k1 expired after 3s', $passed);
$allPassed = $allPassed && $passed;

$result = $ac3->key_get('t10_stagger', 'k2');
$passed = $result !== null && verifyContent($result, $content2);
testResult('k2 still exists after 3s', $passed);
$allPassed = $allPassed && $passed;

$result = $ac3->key_get('t10_stagger', 'k3');
$passed = $result !== null && verifyContent($result, $content3);
testResult('k3 still exists after 3s', $passed);
$allPassed = $allPassed && $passed;

// Wait 3 more seconds - k2 should expire, k3 should still exist
echo "  Waiting 3 more seconds...\n";
sleep(3);

$result = $ac3->key_get('t10_stagger', 'k2');
$passed = $result === null;
testResult('k2 expired after 6s', $passed);
$allPassed = $allPassed && $passed;

$result = $ac3->key_get('t10_stagger', 'k3');
$passed = $result !== null && verifyContent($result, $content3);
testResult('k3 still exists after 6s', $passed);
$allPassed = $allPassed && $passed;

echo "\n";
if ($allPassed) {
    echo "test_10_content_expired_while_reading: ALL TESTS PASSED\n";
    exit(0);
} else {
    echo "test_10_content_expired_while_reading: SOME TESTS FAILED\n";
    exit(1);
}