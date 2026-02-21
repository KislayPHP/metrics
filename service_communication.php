<?php
// php -d extension=modules/kislayphp_metrics.so service_communication.php

if (!extension_loaded('kislayphp_metrics')) {
    fwrite(STDERR, "kislayphp_metrics extension is not loaded\n");
    exit(1);
}

$metrics = new Kislay\Metrics\Metrics();

function recordServiceCall(Kislay\Metrics\Metrics $metrics, string $from, string $to, bool $ok, int $latencyMs): void
{
    $base = "svc.call";
    $path = "{$from}.{$to}";
    $metrics->inc("{$base}.total.{$path}", 1);
    $metrics->inc($ok ? "{$base}.ok.{$path}" : "{$base}.error.{$path}", 1);
    $metrics->inc("{$base}.latency_ms.sum.{$path}", $latencyMs);
    $metrics->inc("{$base}.latency_ms.count.{$path}", 1);
}

recordServiceCall($metrics, 'orders', 'inventory', true, 12);
recordServiceCall($metrics, 'orders', 'inventory', false, 41);
recordServiceCall($metrics, 'orders', 'payment', true, 25);

$all = $metrics->all();
$invCount = max(1, (int)($all['svc.call.latency_ms.count.orders.inventory'] ?? 1));
$invSum = (int)($all['svc.call.latency_ms.sum.orders.inventory'] ?? 0);

echo json_encode([
    'counters' => $all,
    'inventory_avg_latency_ms' => $invSum / $invCount,
], JSON_PRETTY_PRINT) . PHP_EOL;
