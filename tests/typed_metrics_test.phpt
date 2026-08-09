--TEST--
Kislay Metrics Counter, Gauge, Histogram, and Timer classes behave correctly
--EXTENSIONS--
kislayphp_metrics
--FILE--
<?php
use Kislay\Metrics\Counter;
use Kislay\Metrics\Gauge;
use Kislay\Metrics\Histogram;
use Kislay\Metrics\Timer;

// Counter
$c = new Counter('requests_total');
$c->increment();
$c->increment(4);
echo "counter after +1,+4: " . $c->get() . "\n";
$c->decrement(2);
echo "counter after -2: " . $c->get() . "\n";
$c->reset();
echo "counter after reset: " . $c->get() . "\n";

// Gauge - set() is reset-then-inc, not a raw assignment; a stale watcher
// reading mid-set only ever sees 0 or the final value, never a half state.
$g = new Gauge('active_connections');
$g->set(10);
echo "gauge after set(10): " . $g->get() . "\n";
$g->increment(3);
echo "gauge after +3: " . $g->get() . "\n";
$g->decrement(5);
echo "gauge after -5: " . $g->get() . "\n";
$g->set(7);
echo "gauge after set(7): " . $g->get() . "\n";

// Histogram
$h = new Histogram('request_duration_ms', [10, 50, 100]);
$h->observe(5);   // falls in the <=10 bucket
$h->observe(30);  // falls in the <=50 bucket
$h->observe(30);  // same bucket again
$h->observe(500); // past every bucket - counts toward the total only
echo "histogram count: " . $h->getCount() . "\n";
echo "histogram sum: " . $h->getSum() . "\n";
echo "histogram buckets: " . implode(',', $h->getBuckets()) . "\n";
$export = $h->export();
echo "export has HELP line: " . (str_contains($export, '# HELP request_duration_ms') ? 'yes' : 'no') . "\n";
echo "export has bucket le=50: " . (str_contains($export, 'le="50"') ? 'yes' : 'no') . "\n";

// Timer, tied to a histogram - stop() must both return elapsed time AND
// record it into the histogram, and calling stop() twice without a second
// start() must not double-record (guarded by the running flag).
$timerHist = new Histogram('op_duration_ms');
$t = new Timer('op', $timerHist);
$t->start();
usleep(2000);
$elapsed = $t->stop();
echo "timer elapsed > 0: " . ($elapsed > 0 ? 'yes' : 'no') . "\n";
echo "timer recorded into histogram: " . ($timerHist->getCount() === 1 ? 'yes' : 'no') . "\n";
$again = $t->stop();
echo "stop() without start() returns -1: " . ($again === -1.0 ? 'yes' : 'no') . "\n";
echo "histogram count unchanged by the no-op stop: " . ($timerHist->getCount() === 1 ? 'yes' : 'no') . "\n";

// Timer::record() without start()/stop() - direct value injection.
$directHist = new Histogram('direct_ms');
$t2 = new Timer('op2', $directHist);
$t2->record(42.0);
echo "record() without start pushed into histogram: " . ($directHist->getCount() === 1 && $directHist->getSum() === 42.0 ? 'yes' : 'no') . "\n";
?>
--EXPECT--
counter after +1,+4: 5
counter after -2: 3
counter after reset: 0
gauge after set(10): 10
gauge after +3: 13
gauge after -5: 8
gauge after set(7): 7
histogram count: 4
histogram sum: 565
histogram buckets: 10,50,100
export has HELP line: yes
export has bucket le=50: yes
timer elapsed > 0: yes
timer recorded into histogram: yes
stop() without start() returns -1: yes
histogram count unchanged by the no-op stop: yes
record() without start pushed into histogram: yes
