# Bosphorus development notes

## Building
- Build in `build/` with `make -j4` (never `-j$(nproc)`).
- CryptoMiniSat is fetched with FetchContent; when a CMake file changes and
  there is no network, reconfigure with
  `cmake -DFETCHCONTENT_UPDATES_DISCONNECTED=ON ..` so the existing checkout is used.
- The build already uses `-fno-omit-frame-pointer`, so frame-pointer call graphs work.

## Profiling
Always profile before optimising. Use perf with frame-pointer call graphs:

```
perf record --call-graph=fp -o /tmp/perf.data ./build/bosphorus input.anf --el 0 --cnfwrite /dev/null
perf report -i /tmp/perf.data --no-children --percent-limit 1 --stdio | head -80   # self time, hottest symbols
perf report -i /tmp/perf.data --children --percent-limit 2 --stdio | head -120     # inclusive, by call chain
```

`--no-children` shows where time is actually spent (leaf symbols); `--children`
shows which of our functions (rules) sit above the hot leaves. libbrial (the
ZDD library) is built without frame pointers, so with `--call-graph=fp` its
time ends up under "[unknown]" and is not attributed to our functions: when
ZDD symbols dominate, record with `--call-graph=dwarf,16384` instead (slower
to record, but the chains through libbrial are complete). Big ANFs (the
bivium family) have polynomials with thousands of terms, so ZDD operations of
PolyBoRi/CUDD tend to dominate; check which rule calls them.

## Rules
- **No wall-clock or CPU-time budgets inside algorithms.** Limit work with
  deterministic counters (steps, S-polynomials, monomial visits, iterations,
  e.g. `--gbsteps`, `--shortenbudget`), never with seconds: time limits make
  runs irreproducible and bugs impossible to replay. The only time limit is the
  user's global `--maxtime`.
- **Fuzz before every commit**: `python3 utils/fuzz.py --iters 60` (about 15 s)
  generates random small ANF and CNF inputs with random option settings and
  checks Bosphorus's solutions/CNF against brute force. Commit only when it
  reports no failures; a failing case is saved under `fuzz-fail-*` for replay.

## Testing
- `cd build && ctest` runs the lit suite (`tests/anf-files`); `lit -v build/tests/anf-files --filter NAME` for one test.
- End-to-end tests verify against brute force (`tests/utils/verify_anf.py`).

## Benchmarking discipline
- CryptoMiniSat times vary 2-3x between seeds (`--random N`): never conclude
  from one run; compare several seeds or many instances.
- Benchmark a snapshot copy of the binary and library (`cp build/bosphorus
  build/lib/libbosphorus.so* somewhere/; LD_LIBRARY_PATH=somewhere`) so that
  rebuilding during a batch does not change (or break) what is measured.
- Long batches: run them detached (`setsid nohup script > log 2>&1 &`); the
  harness kills background tasks as "low memory" when big CNF files fill the
  page cache. Verify every model against the original ANF.
- A `c p show` line in a CNF makes CryptoMiniSat treat those variables as a
  sampling set: it never eliminates them, and it uses a Gauss-Jordan matrix
  only if >= 60% of the sampling variables occur in it (matrixfinder.cpp).
  On bivium that skipped Gauss-Jordan (which otherwise makes CMS time out on
  every encoding tried; Gauss off: 9 s), on ascon the line costs 2-3x.
  `--projshow 2` (default) writes the line only when the input ANF carried a
  projection set (the CMS heuristic is to be changed on the CMS side). Never
  compare a CNF with the line against one without, and check the
  `[matrix] Good/UNused` lines of the CMS log to know whether Gauss-Jordan
  was in use.
- Do not run two CPU-heavy things at once on this 2-core machine while a
  benchmark batch is running (perf profiles, builds with -j4 distort timings).

## Benchmarks
- The bivium family lives in `/home/soos/development/sat_solvers/xnf/xorricane-bench/bivium/`.
  Run Bosphorus with `--el 0` there; every `.anf` starts with a variable-list line.
- Compare with CryptoMiniSat using the harness flags:
  `cryptominisat5 --sls 0 --autodisablegauss 0 --presimp 1 --maxmatrixrows 100000 --maxmatrixcols 100000 --maxnummatrices 1000000 --minmatrixrows 1`
  and always verify the model against the original ANF (with `--projshow 1`
  CMS prints only the projected variables; complete the rest by unit
  propagation over the CNF).
- The ascon family (`.../xorricane-bench/ascon/`, named variables, 50
  instances, raw-CNF CMS results in `*.cnf.out-cms`) is the second reference
  family; the four-round instances (tmp3g3f82vv, tmpgmh2blh0, ...) are the
  informative ones.
