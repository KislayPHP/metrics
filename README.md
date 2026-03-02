# KislayPHP Metrics

Metrics runtime for KislayPHP.

Primary runtime namespace is `Kislay\Metrics` with backward-compatible aliases under `KislayPHP\Metrics`.

## Concurrency Mode

- Default API mode is synchronous.
- Metric operations are immediate and return scalar/array values.
- Optional RPC transport can be enabled at build/runtime, but API behavior remains synchronous.

This keeps instrumentation predictable and low-overhead for request handlers and workers.

## Installation

```bash
pie install kislayphp/metrics:0.0.2
```

Enable in `php.ini`:

```ini
extension=kislayphp_metrics.so
```

## Public API

`Kislay\Metrics\Metrics`:

- `__construct()`
- `setClient(Kislay\Metrics\ClientInterface $client): bool`
- `inc(string $name, int $by = 1): bool`
- `dec(string $name, int $by = 1): bool`
- `get(string $name): int`
- `all(): array`
- `reset(?string $name = null): bool`

`Kislay\Metrics\ClientInterface`:

- `inc(string $name, int $by = 1): bool`
- `get(string $name): int`
- `all(): array`

Legacy aliases:

- `KislayPHP\Metrics\Metrics`
- `KislayPHP\Metrics\ClientInterface`

## Quick Start

```php
<?php

$metrics = new Kislay\Metrics\Metrics();

$metrics->inc('requests_total');
$metrics->inc('requests_total', 2);
$metrics->dec('requests_total');

var_dump($metrics->get('requests_total'));
var_dump($metrics->all());
```

## Notes

- Keep metric names stable and low-cardinality.
- Use `reset()` for controlled test or dev scenarios, not as normal production flow.
- For inter-service instrumentation policy, see `SERVICE_COMMUNICATION.md`.

