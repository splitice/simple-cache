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
for($i=0;$i<20;$i++){
    $pid = pcntl_fork();
    if(!$pid){
        $ac = new ApiClient("http://127.0.0.1:8081");


        $i = -1;

        $ch = $ac->ch();
        $aborted = false;
        curl_setopt($ch, CURLOPT_PROGRESSFUNCTION, function() use (&$aborted){
            if(rand(0,10) == 3) {
                $aborted = true;
                exit (0);
            }
        });
        curl_setopt($ch, CURLOPT_NOPROGRESS, false);

        try {
            $content = generateRandomString(rand(100, 2000) * 1000);
            for($i=0;$i<100;$i++){
                $ac->key_put('test_4', rand(0,100), $content);
            }
        } catch(\Exception $ex){
            if(!$aborted) throw $ex;
        }

        exit(0);
    }
    $pids[] = $pid;
}

while(pcntl_wait($status) <= 0){
   if($status){
       echo "Error!";
       exit($status);
   }
}
echo "All PIDs done\n";