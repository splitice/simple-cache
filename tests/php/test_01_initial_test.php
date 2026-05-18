<?php
use Splitice\SimpleCache\ApiClient;

require ("vendor/autoload.php");


$host = $argv[1] ?? '127.0.0.1';
$port = (int)($argv[2] ?? 8081);
$ac = new ApiClient("http://$host:$port");

$ret = $ac->key_get("non-existant", "its-a-404");
if($ret !== null) exit(1);

