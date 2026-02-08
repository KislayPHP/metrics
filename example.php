<?php
// Run from this folder with:
// php -d extension=modules/kislayphp_metrics.so example.php

extension_loaded('kislayphp_metrics') or die('kislayphp_metrics not loaded');

$metrics = new KislayPHP\Metrics\Metrics();
$metrics->inc('requests');
$metrics->inc('requests', 2);

var_dump($metrics->get('requests'));
print_r($metrics->all());
