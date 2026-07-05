# BenchX

BenchX is a configurable OLTP workload generator for benchmarking geo-distributed databases. It is built on top of the TPC-C schema, and introduces a wide range of knobs to control the transaction mix, operation intensity, geo-distribution profile (i.e., percentage of multi-home, single-home, and foreign single-home), dependent transactions, data skew, and temporal dynamics.

Thanks to its configurability, BenchX covers a significant number of commonly used OLTP workload generators such as TPC-C, YCSB+T, PPS, SmallBank, MovR, DS Movie, and DS Hotels. Namely, with the right configuration, it replicates the effect of each of them. In addition, it reaches regions of the workload design space that none of them cover.

## Example of Analysis

For example, we can use BenchX to analyze the effect of varying both the geo-distribution profile and the operation intensity on the throughput and latency of several databases, including Detock, SLOG, Calvin, Janus, and CockroachDB.

<p align="center"><img src="Example.svg"></p>

## Running BenchX

BenchX runs on top of the Gaia geo-distributed evaluation framework. BenchX is configured through a single YAML file that is picked up by the workload generator in each region.
```yaml
{
    transaction_mix: "50:25:10:10:5",
    intensity: "10:5:10000:2:1",
    mp_percent: "50:50:100:25:50",
    fsh_percent: "33:33:0:20:50",
    mh_percent: "33:0:20:0:50",
    dependent_percent: "50:50:0:0:0",
    skew: "0.5:0.0:0.9:0.0:0.999",
    temporal_dynamics: "10:90,50:50,90:10"
}
```
<p align="center"><img src="BenchX.svg" width="400"></p>