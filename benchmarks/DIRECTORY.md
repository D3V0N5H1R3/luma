# Benchmarks

Micro-benchmarks for the Luma interpreter, written in Luma itself.

## Running

Run the full suite from the repository root:

```bash
build/luma benchmarks/suite.luma
```

> **Note:** On Windows the binary is `build\Release\luma.exe`; substitute it for `build/luma` in every command below.

Individual `bench_*.luma` files are library modules included by `suite.luma` — they cannot be run standalone.

## Structure

| File                     | Purpose                                        |
| ------------------------ | ---------------------------------------------- |
| `suite.luma`             | Entry point — includes and runs all benchmarks |
| `benchmark_harness.luma` | Timing and iteration helpers (`time_it`, etc.) |
| `bench_*.luma`           | One file per topic (arithmetic, strings, …)    |

## Adding a Benchmark

1. Create a new `bench_<topic>.luma` file.
2. Define an entry function `function BenchResult run_<topic>_benchmarks(BenchResult state)` that calls `time_it` for each case. Thread the result through with the `st = time_it(st, …)` pattern and return the final `BenchResult` (see `bench_arithmetic.luma` for a minimal example).
3. Prefix every other top-level helper in the file with `bench_` (e.g. `bench_square`, `bench_build_chain_graph`). All `bench_*.luma` modules are textually included into a single compilation unit, so they share one global namespace — an unprefixed helper such as `square` or `increment` will eventually collide with an identically named helper in another module and fail to compile in an unexpected file. The `run_<topic>_benchmarks` entry points, the `BenchResult` record, the `time_it` harness, and `Bench*` types are the only top-level names exempt from the prefix. If two helpers would still collide after prefixing, disambiguate the name (e.g. `bench_task_increment` alongside `bench_increment`).
4. In `suite.luma`, add an `include` for the new file **and** a matching `st = run_<topic>_benchmarks(st)` call in `run_benchmarks()`. Luma cannot glob includes, so both lists are maintained by hand — keep them in the same order and update both places together. `scripts/check_benchmark_suite.py` (run in the Benchmark CI workflow) fails the build if a module is missing from either list, turning a silent omission into a caught error.

## Baseline Comparison

The CI workflow automatically tracks performance regressions:

1. After each run, results are parsed into JSON (benchmark name → ms/iter).
2. The JSON is cached as the baseline for the next run.
3. On the first run (no cached baseline), comparison is skipped and results become the initial baseline.
4. Subsequent runs compare against the cached baseline and fail if a CPU-bound benchmark regresses by more than 10%. High-variance I/O and concurrency cases use a wider 50% threshold (see [Local comparison](#local-comparison)).

To force a baseline refresh (e.g. after an intentional performance change), trigger the workflow manually with **Update baseline** enabled.

Every run also uploads `benchmark-results.txt` and `benchmark-results.json` as workflow artifacts (90-day retention) for offline inspection.

### Local comparison

```bash
# Run benchmarks and capture output
build/luma benchmarks/suite.luma | tee results.txt

# Parse to JSON
python3 scripts/parse_benchmark_results.py results.txt -o current.json

# Compare against a saved baseline
python3 scripts/compare_benchmark_results.py baseline.json current.json --threshold 10
```

`compare_benchmark_results.py` applies a wider tolerance to inherently noisy benchmarks
(filesystem, key-value store, `Process.run`, and concurrency/channel/task cases),
controlled by `--io-threshold` (default 50%). This avoids false regressions from
I/O and scheduling jitter while keeping the strict `--threshold` for CPU-bound
cases.

## Warmup

`time_it` runs a warmup phase before starting the timer. The warmup count is 10% of the requested iterations (minimum 1). These warmup iterations stabilise caches and are **not** included in the reported timing.

## Timer Resolution and Auto-Scaling

`DateTime.milliseconds_since_start()` ticks in whole milliseconds, so a timed run of only a few milliseconds is dominated by quantisation error (±1 ms is ±50% of a 2 ms run). `time_it` therefore treats the requested iteration count as a **minimum**: if the timed run finishes in under 30 ms, it is repeated with a proportionally larger count until it spans at least 30 ms (about ±3% quantisation error), capped at 2,000,000 iterations. The `iterations` column in the output is the count that was actually timed, so `ms/iter` is always `total / iterations`.

## Iteration Limits

The VM aborts any single loop that exceeds 10 million iterations. The counter resets whenever a `for` loop starts, so the limit applies to each timed loop individually rather than to the suite as a whole; the auto-scaling cap keeps every timed loop far below it.

## Choosing Iteration Counts

Because short runs are scaled up automatically, the requested count only
needs to be large enough to amortise per-case overhead and to keep cheap cases
from running a wasted calibration pass. As a rule of thumb:

- **Cheap, pure operations** (arithmetic, control-flow branching, function-call
  overhead, single stdlib calls, and value/record/tuple construction):
  high counts — usually `5000`–`10000`, rising to `50000` for the very cheapest
  cases (raw arithmetic, field access).
- **Heavier in-memory work** (building larger collections, multi-step pipelines,
  graph/tree traversal): mid-range counts (`1000`–`5000`).
- **I/O, process spawning, and concurrency** (`Process.run`, FileSystem,
  KeyValueStore, channels, tasks): low counts (often `5`–`100`). These are
  variance-dominated, so more iterations would mostly add noise and wall-clock
  time rather than precision — hence the separate `--io-threshold` in the
  comparison script.

Keep inputs that the case does not set out to measure out of the timed body:
build them once beforehand and capture them in the lambda. Collections are
values (operations return new copies), so a benchmark that mutates a hoisted
collection must thread the returned value rather than discard it — for example,
`KeyValueStore.set` returns a new store and leaves its input untouched.

## Output Format

`time_it` prints one line per benchmark in a fixed, parser-stable format:

```text
<name> | <iterations> iterations | <total_ms> ms | <per_iter_ms> ms/iter
```

`scripts/parse_benchmark_results.py` depends on this exact shape. The per-iteration
value is rounded to 6 decimal places so that sub-microsecond operations remain
distinguishable instead of collapsing to `0.001`. Luma prints full decimal
numbers with no scientific notation, so the `[\d.]+` parser pattern stays valid.
If you change the harness output, update the parser regex to match.
