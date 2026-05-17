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
        $host = $argv[1] ?? '127.0.0.1';
        $port = (int)($argv[2] ?? 8081);
        $ac = new ApiClient("http://$host:$port");


        $i = -1;

        $ch = $ac->getCurlHandle();
        $aborted = false;
        curl_setopt($ch, CURLOPT_PROGRESSFUNCTION, function() use (&$aborted){
            if(rand(0,10) == 3) {
                $aborted = true;
                return 2; // Return non-zero to abort the request
            }
        });
        curl_setopt($ch, CURLOPT_NOPROGRESS, false);

        try {
            $content = generateRandomString(rand(100, 2000) * 1000);
            for($i=0;$i<100;$i++){
                try {
                    $ac->key_put('test_4', rand(0,100), $content);
                } catch(\Exception $ex){
                    if(!$aborted) throw $ex;
                }
            }
        } catch(\Exception $ex){
            if(!$aborted) throw $ex;
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
