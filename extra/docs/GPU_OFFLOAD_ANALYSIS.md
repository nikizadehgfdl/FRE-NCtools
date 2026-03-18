# GPU Offloading Analysis: `compute_atm_lnd_ocn_exchange()` Inner `la` Loop

## Target Loop Structure

**File**: `src/make-coupler-mosaic/make_coupler_mosaic.c`  
**Function**: `compute_atm_lnd_ocn_exchange()`  
**Main Loop**: Line ~2095 (`for (la = is; la <= ie; la++)`)

### Loop Hierarchy
```
for (na = 0; na < ntile_atm; na++)  [outer - stay on CPU]
  ├─ domain2D setup, grid range calculations
  └─ for (la = is; la <= ie; la++)  [inner - OFFLOAD TO GPU]
       ├─ for (nl = 0; nl < ntile_lnd; nl++)
       │   └─ for (jl, il in lnd_range)
       │       └─ clip_2dx2d / clip_2dx2d_great_circle()
       │       └─ accumulate → axl_area[count], ...
       │
       ├─ for (no = 0; no < ntile_ocn; no++)
       │   └─ for (jo, io in ocn_range)
       │       ├─ clip_2dx2d / clip_2dx2d_great_circle()
       │       ├─ WRITE → atmxocn_area[na][no][naxo[na][no]] (RACE!)
       │       ├─ ATOMIC INCREMENT → naxo[na][no]++ (RACE!)
       │       └─ for (l = 0; l < count; l++)
       │           └─ clip lnd-ocn, accumulate axl_*
       │
       └─ post-process: write atmxlnd_* from axl_* via naxl (RACE!)
```

---

## Data Dependency Classification

### 1. **Read-Only (Grid Data) - COPY TO GPU ONCE**
- `xatm[], yatm[], cart_xatm[], cart_yatm[], cart_zatm[]` — atmosphere supergrid
- `xlnd[], ylnd[], cart_xlnd[], cart_ylnd[], cart_zlnd[]` — land supergrid
- `xocn[], yocn[], cart_xocn[], cart_yocn[], cart_zocn[]` — ocean supergrid
- `area_atm[], area_lnd[], area_ocn[]` — grid cell areas
- `omask[]` — ocean/land mask fractions
- `nxa[], nya[], nxl[], nyl[], nxo[], nyo[]` — grid dimensions
- Scalar parameters: `ntile_lnd, ntile_ocn, ntile_atm, clip_method, interp_order, area_ratio_thresh, lnd_same_as_atm, ocn_same_as_atm`

### 2. **Scalars & Work Arrays (Thread-Private) - ALLOCATE ON GPU**
- Loop indices: `ia, ja, il, jl, io, jo, l, n, la, nl, no`
- Clip output: `xa, ya, za, xl, yl, zl, xo, yo, zo` (4×float arrays)
- Area/centroid: `xarea, min_area, yy`
- Clip result: `x_out[], y_out[], z_out[], n_out`
- Fix-lon state: `na_in, nl_in, no_in, ya_min, ya_max, xa_min, xa_max, xl_min, xl_max, xo_min, xo_max`
- Local indices: `axl_i[], axl_j[], axl_t[], num_v[]` (per `count`)
- Local accumulators: `axl_area[], axl_clon[], axl_clat[]` (per `count`)

### 3. **Shared with Race Condition (Atomic Updates) - NEEDS SYNCHRONIZATION**

#### 3a. Ocean Exchange Grid (`atmxocn_*`)
```
if (xarea > threshold) {
  atmxocn_area[na][no][naxo[na][no]] = xarea;          // WRITE
  atmxocn_io[na][no][naxo[na][no]] = io;               // WRITE
  atmxocn_jo[na][no][naxo[na][no]] = jo;               // WRITE
  atmxocn_ia[na][no][naxo[na][no]] = ia;               // WRITE
  atmxocn_ja[na][no][naxo[na][no]] = ja;               // WRITE
  if (interp_order == 2) {
    atmxocn_clon[na][no][naxo[na][no]] = ...;          // WRITE
    atmxocn_clat[na][no][naxo[na][no]] = ...;          // WRITE
  }
  ++(naxo[na][no]);                                      // ATOMIC ++
}
```
**Issue**: Multiple `la` threads compete to increment `naxo[na][no]` and write to overlapping index ranges.

#### 3b. Land Exchange Grid (`atmxlnd_*`)
```
// After post-processing axl_* arrays, write final results:
if (axl_area[l] > threshold) {
  atmxlnd_area[na][nl][naxl[na][nl]] = axl_area[l];    // WRITE
  atmxlnd_ia[na][nl][naxl[na][nl]] = ia;               // WRITE
  ...
  ++(naxl[na][nl]);                                      // ATOMIC ++
}
```
**Issue**: Same race condition as `atmxocn_*`.

---

## GPU Implementation Strategy

### **Option A: Atomic Increment (Simpler, Potentially Slower)**

```c
#pragma acc parallel loop independent private(ia, ja, ...) \
                             reduction(max:naxo_local_max)
for (la = is; la <= ie; la++) {
  // Extract atm grid, compute clips...
  
  if (xarea > threshold) {
    int idx;
    #pragma acc atomic capture
    {
      idx = naxo[na][no];
      naxo[na][no]++;
    }
    if (idx < get_maxxgrid()) {
      atmxocn_area[na][no][idx] = xarea;
      atmxocn_io[na][no][idx] = io;
      // ... other writes
    }
  }
}
```

**Pros**:
- Straightforward annotation
- No restructuring needed
- Works with existing data layout

**Cons**:
- Atomic ops on GPU can be bottlenecks (serializes writes)
- Potential slowdown if `naxo[]` updates are frequent

---

### **Option B: Thread-Local Accumulation (Faster, More Complex)**

Restructure to collect results in local arrays per thread, then merge on CPU:

```c
// On GPU:
#pragma acc parallel loop independent
for (la = is; la <= ie; la++) {
  // Use thread-local buffers:
  // local_atmxocn_area[thread_id][local_count]
  // local_naxo[thread_id] (count for this thread)
  
  // Collect exchange grids locally, no atomics
  if (xarea > threshold) {
    if (local_naxo[thread_id] < MAX_LOCAL_PER_THREAD) {
      local_atmxocn_area[thread_id][local_naxo[thread_id]] = xarea;
      local_naxo[thread_id]++;
    }
  }
}

// On CPU (post-kernel):
for (int t = 0; t < num_threads; t++) {
  for (int i = 0; i < local_naxo[t]; i++) {
    int global_idx = naxo[na][no]++;
    atmxocn_area[na][no][global_idx] = local_atmxocn_area[t][i];
    ...
  }
}
```

**Pros**:
- No atomic operations → better GPU utilization
- Fine-grained parallelism preserved
- Faster on GPUs with many threads

**Cons**:
- Requires significant code restructuring
- Temporary memory overhead (thread-local buffers)
- Post-processing merges on CPU

---

## Recommended Approach

**Start with Option A (Atomic Increment)** for:
- Faster implementation (Phase 2)
- Easier correctness verification
- Simpler fallback to CPU if issues arise

**Optimize to Option B later** if profiling shows atomic contention is a bottleneck.

---

## Function Compatibility with GPU

### Must Mark as `#pragma acc routine seq` (or `gang`, `worker`, `vector`)
- `fix_lon()` — Loop that processes array elements
- `clip_2dx2d()` — Core clipping algorithm (polynomial boundary traversal)
- `clip_2dx2d_great_circle()` — Great-circle variant
- `poly_area()` — Polygon area calculation
- `poly_ctrlon()` — Centroid longitude
- `poly_ctrlat()` — Centroid latitude
- `minval_double()`, `maxval_double()`, `avgval_double()` — Reduction helpers
- `great_circle_area()` — Great-circle area

### Host-Only (printf, file I/O, error handling)
- `print_*()` calls (inside `if (print_grid)` blocks)
- `mpp_error()` calls (error conditions)
- These can stay on host via conditional `#pragma acc host_data` or skip on GPU

---

## Data Movement Strategy

### Phase 4.1: Data Copyin (Once per `na` iteration)
```c
#pragma acc data copyin(xatm[na:1][0:nxa[na]*(nya[na]+1)], ...) \
                copyin(xlnd[0:ntile_lnd][0:...], ...) \
                copyin(xocn[0:ntile_ocn][0:...], ...) \
                copy(naxo[na:1][0:ntile_ocn], ...) \
                copy(atmxocn_area[na:1][0:ntile_ocn][0:get_maxxgrid()])
{
  #pragma acc parallel loop independent
  for (la = is; la <= ie; la++) {
    // ... kernel code
  }
}
```

### Phase 4.2: Copy Results Back (After `la` loop)
```
#pragma acc update host(naxo[na:1][0:ntile_ocn], \
                        atmxocn_area[na:1][0:ntile_ocn][0:...], ...)
```

---

## Verification Steps

1. **Compilation**: `nvc -acc=gpu -gpu=cc80 -c make_coupler_mosaic.c`
2. **Unit test**: Compare GPU vs. CPU output on small C48 grid
3. **Correctness**: NetCDF output byte-for-byte match (within floating-point tolerance)
4. **Profiling**: `nsys profile --stats true ./make_coupler_mosaic` to identify bottlenecks

---

## Known Challenges

1. **printf Debugging**: `printf` may not work reliably on GPU. Use `#pragma acc routine host` wrappers.
2. **Large Arrays**: Exchange grid arrays can exceed GPU memory. May need unified memory or host-resident arrays.
3. **Function Inlining**: Compiler may not inline all marked routines. Consider manual inlining of hot functions.
4. **Floating-Point Precision**: Accumulated polygon areas may differ slightly from CPU due to FP rounding order.

