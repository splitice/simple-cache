<?php

use Splitice\SimpleCache\ApiClient;

require ("vendor/autoload.php");

function generateRandomString($length = 10) {
    $characters = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ';
    $charactersLength = strlen($characters);
    $randomString = '';
    for ($i = 0; $i < $length; $i++) {
        $randomString .= $characters[rand(0, $charactersLength - 1)];
    }
    return $randomString;
}

$pids = [];
for($i=0;$i<3;$i++){
    $pid = pcntl_fork();
    if(!$pid){
        $ac = new ApiClient("http://127.0.0.1:8081");


        for($f=0;$f<20;$f++){
            $content = generateRandomString(rand(100, 1000) * ($f + 1));
            $ac->key_put('test_3','a', $content);
            echo "Written $f\n";
        }

        exit(0);
    }
    $pids[] = $pid;
}

foreach($pids as $pid){
    pcntl_waitpid($pid, $status);
    if($status){
        echo "Error!";
        exit($status);
    }
}
echo "All PIDs done\n";