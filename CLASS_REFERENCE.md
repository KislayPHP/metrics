# Metrics Class Reference

Runtime classes exported by `kislayphp/metrics`.

## Namespace

- Primary: `Kislay\\Metrics`
- Legacy alias: `KislayPHP\\Metrics`

## `Kislay\\Metrics\\ClientInterface`

Contract for custom metrics backends.

- `inc(string $name, ?int $by = null)`
  - Increase metric value.
- `get(string $name)`
  - Read metric value.
- `all()`
  - Return all metrics.

## `Kislay\\Metrics\\Metrics`

In-process metrics collector with optional backend delegation.

### Constructor

- `__construct()`
  - Create metrics collector.

### Backend Injection

- `setClient(ClientInterface $client)`
  - Attach custom metrics backend.

### Counter and Gauge Operations

- `inc(string $name, ?int $by = null)`
  - Increment metric.
- `dec(string $name, ?int $by = null)`
  - Decrement metric.
- `get(string $name)`
  - Read one metric.
- `all()`
  - Read all metrics.
- `reset(?string $name = null)`
  - Reset one metric or all metrics.
