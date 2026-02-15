<?php
// Run from this folder with:
// php -d extension=modules/kislayphp_metrics.so example.php

function fail(string $message): void {
	echo "FAIL: {$message}\n";
	exit(1);
}

if (!extension_loaded('kislayphp_metrics')) {
	fail('kislayphp_metrics not loaded');
}

$metrics = new KislayPHP\Metrics\Metrics();

class ArrayMetricsClient implements KislayPHP\Metrics\ClientInterface {
	private array $counters = [];

	public function inc(string $name, ?int $by = 1): bool {
		$this->counters[$name] = ($this->counters[$name] ?? 0) + ($by ?? 1);
		return true;
	}

	public function get(string $name): int {
		return (int)($this->counters[$name] ?? 0);
	}

	public function all(): array {
		return $this->counters;
	}
}

$metrics->setClient(new ArrayMetricsClient());

$metrics->inc('requests');
$metrics->inc('requests', 2);

$value = $metrics->get('requests');
if ($value !== 3) {
	fail('requests counter mismatch');
}

$all = $metrics->all();
if (!is_array($all) || ($all['requests'] ?? null) !== 3) {
	fail('all() missing requests counter');
}

echo "OK: metrics example passed\n";
