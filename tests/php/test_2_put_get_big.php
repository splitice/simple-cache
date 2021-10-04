<?php

use Splitice\SimpleCache\ApiClient;

require ("vendor/autoload.php");

$ac = new ApiClient("http://127.0.0.1:8081");

function generateRandomString($length = 10) {
    $characters = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ';
    $charactersLength = strlen($characters);
    $randomString = '';
    for ($i = 0; $i < $length; $i++) {
        $randomString .= $characters[rand(0, $charactersLength - 1)];
    }
    return $randomString;
}

for($i = 0; $i < 20; $i++) {
    echo "Writing string $i\n";
    $content = generateRandomString(rand(1000, 2000) * 1024);

    $ret = $ac->key_put("t1", $i, $content); 


    $ret = $ac->key_get("t1", $i);
    if($ret != $content) {
        echo "FAILED";
        echo var_dump($ret);
        exit(1);
    }
}