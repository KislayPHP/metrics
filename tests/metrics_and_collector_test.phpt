--TEST--
Kislay Metrics base class and Collector registry behave correctly
--EXTENSIONS--
kislayphp_metrics
--FILE--
<?php
use Kislay\Metrics\Metrics;
use Kislay\Metrics\Collector;

$m = new Metrics();
$m->inc('http_requests_total');
$m->inc('http_requests_total', 4);
$m->inc('errors_total');
echo "http_requests_total: " . $m->get('http_requests_total') . "\n";
echo "errors_total: " . $m->get('errors_total') . "\n";
echo "unknown metric defaults to 0: " . $m->get('never_touched') . "\n";

$m->dec('http_requests_total', 2);
echo "after dec(2): " . $m->get('http_requests_total') . "\n";

$all = $m->all();
ksort($all);
foreach ($all as $name => $value) {
    echo "all[{$name}] = {$value}\n";
}

$m->reset('errors_total');
echo "errors_total after reset: " . $m->get('errors_total') . "\n";
echo "http_requests_total untouched by that reset: " . $m->get('http_requests_total') . "\n";

// Collector: namespaced, idempotent factory methods.
$col = new Collector('demo');
$c1 = $col->counter('requests');
$c1->increment(5);
$c2 = $col->counter('requests'); // same name -> must be the SAME instance
$c2->increment(2);
echo "collector counter is shared across calls: " . ($c1->get() === $c2->get() && $c1->get() === 7 ? 'yes' : 'no') . "\n";

$gauge = $col->gauge('active');
$gauge->set(3);

$hist = $col->histogram('latency_ms', [10, 100]);
$hist->observe(5);

$export = $col->export();
echo "export has namespaced counter: " . (str_contains($export, 'demo_requests') ? 'yes' : 'no') . "\n";
echo "export has namespaced gauge: " . (str_contains($export, 'demo_active') ? 'yes' : 'no') . "\n";
echo "export has namespaced histogram: " . (str_contains($export, 'demo_latency_ms') ? 'yes' : 'no') . "\n";
?>
--EXPECT--
http_requests_total: 5
errors_total: 1
unknown metric defaults to 0: 0
after dec(2): 3
all[errors_total] = 1
all[http_requests_total] = 3
errors_total after reset: 0
http_requests_total untouched by that reset: 3
collector counter is shared across calls: yes
export has namespaced counter: yes
export has namespaced gauge: yes
export has namespaced histogram: yes
