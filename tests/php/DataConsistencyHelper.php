<?php

/**
 * DataConsistencyHelper - Shared utilities for simple-cache consistency tests
 * 
 * Provides deterministic content generation, async PUT with mid-stream pause,
 * async GET, and coordination primitives for concurrency testing.
 * Uses non-blocking sockets to avoid hangs.
 */

define('BLOCK_SIZE', 4096);
define('SMALL_SIZE', 100);
define('LARGE_SIZE', 5000);

function generateKnownContent($length)
{
    $pattern = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
    $result = '';
    for ($i = 0; $i < $length; $i++) {
        $result .= $pattern[$i % strlen($pattern)];
    }
    return $result;
}

function verifyContent($actual, $expected, $label = '')
{
    if ($actual === $expected) return true;
    $prefix = $label ? "[$label] " : "";
    echo "{$prefix}FAILED: Content mismatch\n";
    echo "{$prefix}  Expected length: " . strlen($expected) . "\n";
    echo "{$prefix}  Actual length:   " . (is_string($actual) ? strlen($actual) : 'null') . "\n";
    if (is_string($actual) && strlen($actual) === strlen($expected)) {
        for ($i = 0; $i < strlen($expected); $i++) {
            if ($actual[$i] !== $expected[$i]) {
                echo "{$prefix}  First diff at byte $i\n";
                break;
            }
        }
    }
    return false;
}

function assertOrDie($condition, $message)
{
    if (!$condition) {
        echo "FAILED: $message\n";
        exit(1);
    }
}

function rawRequest($host, $port, $request, $timeout = 3)
{
    $sock = @fsockopen($host, $port, $errno, $errstr, 2);
    if (!$sock) return null;
    stream_set_timeout($sock, $timeout);
    fwrite($sock, $request);
    $response = '';
    while (!feof($sock)) {
        $data = @fread($sock, 4096);
        if ($data === false || $data === '') break;
        $response .= $data;
        if (strpos($response, "\r\n\r\n") !== false) {
            if (preg_match('/Content-Length: (\d+)/i', $response, $m)) {
                $he = strpos($response, "\r\n\r\n") + 4;
                if (strlen($response) - $he >= (int)$m[1]) break;
            } else break;
        }
    }
    fclose($sock);
    return $response;
}

function extractBody($response)
{
    $bs = strpos($response, "\r\n\r\n");
    return $bs !== false ? substr($response, $bs + 4) : '';
}

function isHttp200($r)
{
    return $r !== null && strpos($r, '200 OK') !== false;
}
function isHttp404($r)
{
    return $r !== null && (strpos($r, '404') !== false || strpos($r, 'Not Found') !== false);
}

// ============================================================
// Async PUT with pause (non-blocking socket)
// ============================================================

function startAsyncPutWithPause($host, $port, $table, $key, $content)
{
    $sf = tempnam(sys_get_temp_dir(), 'scache_');
    $pid = pcntl_fork();
    if ($pid == -1) {
        echo "FAILED: fork\n";
        exit(1);
    }
    if ($pid == 0) {
        $sock = @fsockopen($host, $port, $errno, $errstr, 5);
        if (!$sock) {
            file_put_contents($sf, "error:$errstr");
            exit(1);
        }
        stream_set_blocking($sock, false);
        $hdr = "PUT /$table/$key HTTP/1.1\r\nHost: $host:$port\r\nConnection: close\r\nContent-Length: " . strlen($content) . "\r\n\r\n";
        $w = 0;
        $hl = strlen($hdr);
        while ($w < $hl) {
            $n = @fwrite($sock, substr($hdr, $w));
            if ($n === false || $n === 0) {
                usleep(10000);
                continue;
            }
            $w += $n;
        }
        file_put_contents($sf, 'ready');
        for ($i = 0; $i < 300; $i++) {
            $s = @file_get_contents($sf);
            if ($s === 'go' || strpos($s, 'kill') === 0) break;
            usleep(50000);
        }
        $s = @file_get_contents($sf);
        if (strpos($s, 'kill') === 0) {
            fclose($sock);
            file_put_contents($sf, 'done:interrupted');
            exit(0);
        }
        $w = 0;
        $bl = strlen($content);
        $st = time();
        while ($w < $bl) {
            $n = @fwrite($sock, substr($content, $w));
            if ($n === false || $n === 0) {
                if (time() - $st > 5) break;
                usleep(10000);
                continue;
            }
            $w += $n;
        }
        $resp = '';
        $st = time();
        while (time() - $st < 3) {
            $d = @fread($sock, 4096);
            if ($d !== false && $d !== '') {
                $resp .= $d;
                if (strpos($resp, "\r\n\r\n") !== false) {
                    if (preg_match('/Content-Length: (\d+)/i', $resp, $m)) {
                        $he = strpos($resp, "\r\n\r\n") + 4;
                        if (strlen($resp) - $he >= (int)$m[1]) break;
                    } else break;
                }
            }
            usleep(10000);
        }
        fclose($sock);
        file_put_contents($sf, 'done:' . $resp);
        exit(0);
    }
    for ($i = 0; $i < 300; $i++) {
        clearstatcache();
        if (!file_exists($sf)) {
            usleep(50000);
            continue;
        }
        $s = @file_get_contents($sf);
        if ($s === 'ready' || strpos($s, 'error:') === 0) break;
        usleep(50000);
    }
    return ['pid' => $pid, 'signalFile' => $sf];
}

function signalAsyncPutContinue($info)
{
    file_put_contents($info['signalFile'], 'go');
}
function signalAsyncPutKill($info)
{
    file_put_contents($info['signalFile'], 'kill');
}

function waitAsyncPut($info)
{
    pcntl_waitpid($info['pid'], $status);
    for ($i = 0; $i < 300; $i++) {
        clearstatcache();
        $c = @file_get_contents($info['signalFile']);
        if ($c === false) {
            usleep(50000);
            continue;
        }
        if (strpos($c, 'done:') === 0) {
            @unlink($info['signalFile']);
            return substr($c, 5);
        }
        if (strpos($c, 'error:') === 0) {
            @unlink($info['signalFile']);
            return null;
        }
        usleep(50000);
    }
    @unlink($info['signalFile']);
    return null;
}

// ============================================================
// Async GET (slow consumer)
// ============================================================

function startAsyncGet($host, $port, $table, $key)
{
    $rf = tempnam(sys_get_temp_dir(), 'scache_get_');
    $pid = pcntl_fork();
    if ($pid == -1) {
        echo "FAILED: fork\n";
        exit(1);
    }
    if ($pid == 0) {
        $sock = @fsockopen($host, $port, $errno, $errstr, 5);
        if (!$sock) {
            file_put_contents($rf, "error:$errstr");
            exit(1);
        }
        stream_set_blocking($sock, false);
        $req = "GET /$table/$key HTTP/1.1\r\nHost: $host:$port\r\nConnection: close\r\n\r\n";
        $w = 0;
        $rl = strlen($req);
        while ($w < $rl) {
            $n = @fwrite($sock, substr($req, $w));
            if ($n === false || $n === 0) {
                usleep(10000);
                continue;
            }
            $w += $n;
        }
        file_put_contents($rf, 'sent');
        $resp = '';
        $st = time();
        while (time() - $st < 5) {
            $d = @fread($sock, 256);
            if ($d !== false && $d !== '') {
                $resp .= $d;
                if (strpos($resp, "\r\n\r\n") !== false) {
                    if (preg_match('/Content-Length: (\d+)/i', $resp, $m)) {
                        $he = strpos($resp, "\r\n\r\n") + 4;
                        if (strlen($resp) - $he >= (int)$m[1]) break;
                    } else break;
                }
            }
            usleep(10000);
        }
        fclose($sock);
        file_put_contents($rf, 'done:' . extractBody($resp));
        exit(0);
    }
    for ($i = 0; $i < 300; $i++) {
        clearstatcache();
        $s = @file_get_contents($rf);
        if ($s === 'sent') break;
        usleep(50000);
    }
    return ['pid' => $pid, 'resultFile' => $rf];
}

function waitAsyncGet($info)
{
    pcntl_waitpid($info['pid'], $status);
    for ($i = 0; $i < 300; $i++) {
        clearstatcache();
        $c = @file_get_contents($info['resultFile']);
        if ($c === false) {
            usleep(50000);
            continue;
        }
        if (strpos($c, 'done:') === 0) {
            @unlink($info['resultFile']);
            return substr($c, 5);
        }
        if (strpos($c, 'error:') === 0) {
            @unlink($info['resultFile']);
            return null;
        }
        usleep(50000);
    }
    @unlink($info['resultFile']);
    return null;
}

// ============================================================
// Utility
// ============================================================

function waitForChild($pid)
{
    pcntl_waitpid($pid, $status);
    return $status;
}

function getServerPid($pf)
{
    if (!file_exists($pf)) return null;
    $p = (int)trim(file_get_contents($pf));
    return $p > 0 ? $p : null;
}

function killServer($pf, $sig = SIGTERM)
{
    $p = getServerPid($pf);
    if ($p) {
        posix_kill($p, $sig);
        // Wait for graceful shutdown (SIGTERM triggers db_close + connection_cleanup)
        for ($i = 0; $i < 50; $i++) {
            if (!posix_kill($p, 0)) break; // process exited
            usleep(100000);
        }
        // If still alive after 5s, force kill
        if (posix_kill($p, 0)) {
            posix_kill($p, SIGKILL);
            for ($i = 0; $i < 20; $i++) {
                if (!posix_kill($p, 0)) break;
                usleep(100000);
            }
        }
    }
    if (file_exists($pf)) @unlink($pf);
}

function testHeader($name)
{
    echo "\n========================================\nTEST: $name\n========================================\n";
}
function testResult($name, $passed)
{
    echo ($passed ? "PASS" : "FAIL") . ": $name\n";
    return $passed;
}
