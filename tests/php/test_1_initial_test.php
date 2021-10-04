<?php
use Splitice\SimpleCache\ApiClient;

require ("vendor/autoload.php");

$ac = new ApiClient("http://127.0.0.1:8081");

$ret = $ac->key_get("non-existant", "its-a-404");
if($ret !== null) exit(1);

