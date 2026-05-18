<?php

use Splitice\SimpleCache\ApiClient;

require ("vendor/autoload.php");

$pids = [];
for($i=0;$i<100;$i++){
    $pid = pcntl_fork();
    if($pid == 0){
        $host = $argv[1] ?? '127.0.0.1';
        $port = (int)($argv[2] ?? 8081);
        $ac = new ApiClient("http://$host:$port");


        $i = -1;

        $ch = $ac->getCurlHandle();
        curl_setopt($ch, CURLOPT_PROGRESSFUNCTION, function() use ($i){
            if(rand(0,10) == 3)  return 2; // Return non-zero to abort the request
        });
        curl_setopt($ch, CURLOPT_NOPROGRESS, false);

        for($i=0;$i<100;$i++){
            try {
                $ac->key_put('test_4', rand(0,1000), rand(0,1000));
            } catch(\Exception $ex){
                // ignore
            }
        }

        exit(0);
    }
    $pids[] = $pid;
}

foreach($pids as $pid){
    pcntl_waitpid($pid, $status);
    if($status){
        echo "error";
        exit($status);
    }
}
echo "All PIDs done\n";
