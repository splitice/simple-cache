<?php

use Splitice\SimpleCache\ApiClient;

require ("vendor/composer/autoload_real.php");
echo "test of tests";

$ac = new ApiClient("http://127.0.0.1:8081");

$ret = $ac->key_put("t1", "k1", "v1"); 
echo var_dump($ret);


$ret = $ac->key_get("t1", "k1");
if($ret != "v1") {
    echo "FAILED";
    echo var_dump($ret);
    exit(1);
}

