<?php

use Splitice\SimpleCache\ApiClient;

require ("vendor/autoload.php");

for($i=0;$i<100;$i++){
    if(pcntl_fork() == NULL){
        $ac = new ApiClient("http://127.0.0.1:8081");


        $i = -1;

        $ch = $ac->getCurlHandle();
        curl_setopt($ch, CURLOPT_PROGRESSFUNCTION, function() use ($i){
            if(rand(0,10) == 3) exit (0);
        });
        curl_setopt($ch, CURLOPT_NOPROGRESS, false);

        for($i=0;$i<100;$i++){
            $ac->key_put('test_4', rand(0,1000), rand(0,1000));
        }

        exit(0);
    }
}

while(pcntl_wait($status) <= 0){
    if($status){
        echo "error";
        exit($status);
    }
    // nothing
}
echo "All PIDs done\n";