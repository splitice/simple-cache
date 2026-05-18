<?php

use Splitice\SimpleCache\ApiClient;

require ("vendor/autoload.php");



$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);
$ac = new ApiClient("http://$host:$port");

$ret = $ac->key_put("t1", "k1", "v1"); 


$ret = $ac->key_get("t1", "k1");
if($ret != "v1") {
    echo "FAILED";
    var_dump($ret);
    exit(1);
}

