# TPC-C ACID Compliance

Native ACID, consistency, and isolation test harness built on top of the existing TPC-C execution path.

## Key Files

| File | Purpose |
|---|---|
| [execution/tpcc/consistency_check.cpp](execution/tpcc/consistency_check.cpp) | All 12 TPC-C §3.3 consistency checks |
| [execution/write_skew/](execution/write_skew/) | Standalone write-skew workload — schema, transaction, checker, loader |
| [test/execution/tpcc/consistency_check_test.cpp](test/execution/tpcc/consistency_check_test.cpp) | Unit tests for consistency checks and atomicity |
| [test/e2e/consistency_singleregion_test.cpp](test/e2e/consistency_singleregion_test.cpp) | TPC-C §3.3 consistency checks, single cluster |
| [test/e2e/isolation_level_singleregion_test.cpp](test/e2e/isolation_level_singleregion_test.cpp) | Write-skew probe for all 5 protocols, single cluster |
| [test/e2e/singleregion_test_utils.h](test/e2e/singleregion_test_utils.h) | Shared cluster infrastructure for single-cluster tests |
| [test/e2e/consistency_multiregion_test.cpp](test/e2e/consistency_multiregion_test.cpp) | TPC-C §3.3 consistency checks across 2 regions × 2 partitions |
| [test/e2e/isolation_level_multiregion_test.cpp](test/e2e/isolation_level_multiregion_test.cpp) | Write-skew probe with multi-home txns, 2 regions × 2 partitions |
| [test/e2e/multiregion_test_utils.h](test/e2e/multiregion_test_utils.h) | Shared cluster infrastructure for multi-region tests |
| [scripts/acid/](scripts/acid/) | All test runner scripts |

## TPC-C §3 Coverage

| Spec area | Status |
|---|---|
| §3.2.2.1 Payment commit atomicity | Implemented — verifies `WAREHOUSE`, `DISTRICT`, `CUSTOMER`, `HISTORY` updates |
| §3.2.2.2 Payment rollback atomicity | Partial — abort/no-visible-effects via invalid `NewOrder`, not spec-exact ROLLBACK |
| §3.3.2.1–§3.3.2.12 consistency conditions | Implemented — `RunBasicConsistencyChecks(...)` |
| §3.3.3.1 initial consistency | Implemented — `InitialLoadIsConsistent` |
| §3.3.3.2 post-run consistency | Implemented — `RunsConsistentWorkload` |
| §3.4.2 isolation tests 1–9 | Not implemented — custom write-skew probe only |
| §3.5 durability | Not implemented |

## Consistency Conditions (§3.3.2)

| # | Name | Condition |
|---|---|---|
| C1 | `warehouse_ytd_matches_districts` | `W_YTD = sum(D_YTD)` |
| C2 | `district_next_order_matches_orders` | `D_NEXT_O_ID - 1 = max(O_ID) = max(NO_O_ID)` |
| C3 | `new_order_forms_contiguous_suffix` | `NEW_ORDER` rows form a gap-free range |
| C4 | `district_order_line_sum_matches_headers` | `sum(O_OL_CNT) = count(ORDER_LINE)` per district |
| C5 | `order_delivery_state_matches_new_order` | `O_CARRIER_ID` is null iff `NEW_ORDER` row exists |
| C6 | `order_line_count_matches_order_header` | `O_OL_CNT = count(ORDER_LINE)` per order |
| C7 | `order_line_delivery_matches_order` | `OL_DELIVERY_D` is null iff order undelivered |
| C8 | `warehouse_ytd_matches_history` | `W_YTD = sum(H_AMOUNT)` |
| C9 | `district_ytd_matches_history` | `D_YTD = sum(H_AMOUNT)` |
| C10 | `customer_balance_matches_delivered_orders_and_history` | `C_BALANCE = sum(delivered OL_AMOUNT) - sum(H_AMOUNT)` |
| C11 | `order_minus_new_order_equals_2100` | `count(ORDER) - count(NEW_ORDER) = 2100` per district |
| C12 | `customer_balance_plus_ytd_matches_delivered_orders` | `C_BALANCE + C_YTD_PAYMENT = sum(delivered OL_AMOUNT)` |

Extra checks (beyond spec): `customer_ytd_payment_matches_history`, `customer_payment_cnt_matches_history`, `customer_delivery_cnt_matches_orders`.

## How We Test Isolation

### What we actually test: write-skew probe (Berenson et al., SIGMOD 1995 §4.2)

The write-skew anomaly is the canonical boundary between Snapshot Isolation and Serializability. We probe for it using 100 concurrent transaction pairs. For pair `i`:

- **Slot A** (`id = 2i-1`) is owned by transaction A (WRITE).
- **Slot B** (`id = 2i`) is owned by transaction B (WRITE).

Transaction A declares `READ` on slot B and `WRITE` on slot A. Transaction B does the reverse. Both read the other slot, check the invariant `own_val + other_val < 1`, and if true, set their own slot to 1.

```
Txn A: READ slot_B, WRITE slot_A  →  if slot_A + slot_B < 1: slot_A = 1
Txn B: READ slot_A, WRITE slot_B  →  if slot_A + slot_B < 1: slot_B = 1
```

There is **no write-write conflict** between A and B, so the scheduler cannot serialize them by lock alone. Under Snapshot Isolation both see the initial state `(0, 0)`, both satisfy the invariant, and both write 1 — producing `slot_A=1, slot_B=1` (write skew). Under Serializability, whichever runs first sets its slot to 1; the other then reads `1 + 0 = 1 ≥ 1` and does not write — no skew.

After all pairs complete, `RunBasicConsistencyChecks` scans each pair and flags any where `slot_A + slot_B > 1`.

### What this tells us

| Observation | Interpretation |
|---|---|
| YTD / payment-count mismatches | Lost updates — Read Committed or weaker |
| Any pair with both slots = 1 | Write skew — protocol provides at most Snapshot Isolation |
| Order/delivery mismatches, no YTD drift | Ordering anomaly or execution bug |
| No violations | Consistent with Strict Serializability |

### How Each Isolation Level is Tested

#### Read Committed

**What it means:** A transaction sees only committed data, but two reads of the same row within one transaction can return different values (non-repeatable reads). Lost updates are possible because two concurrent transactions can both read an old value, compute independently, and overwrite each other.

**How we detect it:** The 12 TPC-C consistency checks (`RunBasicConsistencyChecks`) serve as lost-update detectors. Conditions C1 (W_YTD = Σ D_YTD), C8 (W_YTD = Σ H_AMOUNT), C9 (D_YTD = Σ H_AMOUNT), C10 (C_BALANCE reconciliation), C12 (C_BALANCE + C_YTD_PAYMENT), and the extra checks `customer_ytd_payment_matches_history`, `customer_payment_cnt_matches_history`, `customer_delivery_cnt_matches_orders` will all drift if Payment transactions overwrite each other's YTD increments. Any non-zero `violations` count in those checks signals Read-Committed-or-weaker behaviour.

**Test:** `ExpectConsistentTPCCState` — runs 500 interleaved `NewOrder` + `Payment` transactions (50 : 50 mix) and asserts every consistency check passes with 0 violations.

```
if (lost_updates_found)
  → "System provides READ COMMITTED or lower isolation"
```

---

#### Snapshot Isolation

**What it means:** Every transaction reads from a consistent snapshot taken at its start time. This eliminates lost updates and non-repeatable reads, but allows write skew: two concurrent transactions can each read disjoint rows, satisfy a shared invariant against the snapshot, and both write — producing a state that violates the invariant under neither write's view individually.

**How we detect it:** The write-skew probe (`ExpectNoWriteSkew`) runs 100 concurrent transaction pairs against the `write_skew` workload (completely separate from TPC-C data). For pair `i`:

- Txn A declares `WRITE` on `id_a = 2i-1` and `READ` on `id_b = 2i`.
- Txn B declares `WRITE` on `id_b` and `READ` on `id_a`.

Both check `own_val + other_val < 1`; if true, set their slot to 1. There is **no write-write conflict**, so a lock-only scheduler cannot order them. After all pairs commit (or abort), `RunWriteSkewCheck` scans the `write_skew_detected` result: any pair where `VALUE(id_a) = 1 ∧ VALUE(id_b) = 1` is a write-skew violation.

```
else if (write_skew_found)
  → "System provides SNAPSHOT ISOLATION"
```

**Negative control:** `ExpectDetectsWriteSkewInStorage` manually inserts `{id=1, VALUE=1}` and `{id=2, VALUE=1}` directly into storage and asserts `write_skew_detected` reports at least one violation — confirming the checker itself is correct before trusting a clean result.

---

#### Strict Serializability

**What it means:** Execution is equivalent to some serial order that also respects real-time ordering. Eliminates all anomalies including write skew.

**How we detect it:** A protocol reaches this classification only when:
1. All 12+ consistency checks pass with 0 violations (no lost updates), AND
2. `write_skew_detected` passes with 0 violations (no write skew), AND
3. No ordering-anomaly checks fire (C2, C3, C5, C6, C7, C11).

```
else (no anomalies)
  → "System provides STRICT SERIALIZABILITY"
```

---

### Observed Results per Protocol (single-cluster)

All five protocols were run with `test_isolation_singleregion.sh`. Each suite exercises three test cases: `RunsConsistentWorkload`, `DetectsWriteSkewInStorage`, and `ExecutionProducesNoWriteSkew`.

| Protocol | Config flags | Passed / Total | Write-skew anomalies | Classification |
|---|---|---|---|---|
| Detock (ddr_ts) | `bypass_mh_orderer=true`, `ddr_interval=100ms`, `timestamp_buffer_us=2000` | 3 / 3 | 0 | Strict Serializability |
| DDR-only | `bypass_mh_orderer=true`, `ddr_interval=100ms` (no timestamp buffer) | 3 / 3 | 0 | Strict Serializability |
| SLOG | `bypass_mh_orderer=false` | 3 / 3 | 0 | Strict Serializability |
| Calvin | `bypass_mh_orderer=false` | 3 / 3 | 0 | Strict Serializability |
| Janus | `num_workers=6` (Janus coordinator/acceptor topology) | 1 / 3 | — | Inconclusive (2 tests failed) |

### What this does NOT test

The official TPC-C isolation tests (§3.4.2, tests 1–9) require pausing a live transaction mid-execution and issuing reconnaissance reads against its intermediate dirty/uncommitted state from a second concurrent transaction. This harness only observes whole-transaction committed outcomes — that control point does not exist. The 9 spec tests (dirty read, fuzzy read, phantom, etc.) are therefore not covered.

## How To Run

All scripts support `--local` to use the existing local build instead of Docker.

```bash
# Run everything
bash scripts/acid/test_acid.sh --local

# Single-cluster consistency only
bash scripts/acid/test_consistency_singleregion.sh --local

# Single-cluster isolation only
bash scripts/acid/test_isolation_singleregion.sh --local

# Multi-cluster consistency only
bash scripts/acid/test_consistency_multiregion.sh --local

# Multi-cluster isolation (not yet implemented, exits immediately)
bash scripts/acid/test_isolation_multiregion.sh --local
```

Narrow a run with `TEST_FILTER`:
```bash
TEST_FILTER='TPCCConsistencyCheckTest.PaymentCommitUpdatesWarehouseDistrictAndCustomer' \
  bash scripts/acid/test_consistency_singleregion.sh --local
```

All output lands under a single `acid_results/` directory (override with `OUTPUT_DIR=my/path`):

```
acid_results/
├── acid_test_summary.csv
├── singleregion_consistency_results.csv
├── singleregion_isolation_results.csv
├── multiregion_consistency_results.csv
├── multiregion_isolation_results.csv
├── singleregion_consistency_logs/
├── singleregion_isolation_logs/
├── multiregion_consistency_logs/
└── multiregion_isolation_logs/
```

## Gaps

1. Official isolation tests §3.4.2 (1–9) — needs live transaction control
2. True pre-COMMIT rollback for §3.2.2.2 atomicity
3. Durability / crash-recovery testing (§3.5)
4. Full distributed Janus validation — `isolation_level_multiregion_test.cpp` now exercises Janus with multi-home transactions across 2 regions × 2 partitions, but end-to-end results are not yet confirmed
