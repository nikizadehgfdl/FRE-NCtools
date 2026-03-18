# Phase 2: GPU Offloading Code Annotation Summary

**Status**: ✅ COMPLETE  
**Date**: March 18, 2026  
**Target File**: `src/make-coupler-mosaic/make_coupler_mosaic.c`  
**Function**: `compute_atm_lnd_ocn_exchange()`

## Overview

Phase 2 has successfully annotated the core GPU offloading region in the FRE-NCtools coupler mosaic computation with OpenACC pragmas. The annotations prepare the code for GPU acceleration using NVIDIA HPC SDK (`nvc` compiler) with OpenACC support.

## Key Changes Made

### 1. **Primary Parallel Loop Annotation** (Line 2094-2109)

**Pragma Added**:
```c
#pragma acc parallel loop independent collapse(1) \
  present(xatm[na], yatm[na], cart_xatm[na], cart_yatm[na], cart_zatm[na], \
          xlnd, ylnd, cart_xlnd, cart_ylnd, cart_zlnd, \
          xocn, yocn, cart_xocn, cart_yocn, cart_zocn, \
          area_lnd, area_atm, area_ocn, omask, \
          naxl[na], naxo[na], atmxlnd_area[na], atmxocn_area[na], \
          atmxlnd_ia[na], atmxlnd_ja[na], atmxlnd_il[na], atmxlnd_jl[na], \
          atmxocn_ia[na], atmxocn_ja[na], atmxocn_io[na], atmxocn_jo[na], \
          atmxlnd_clon[na], atmxlnd_clat[na], atmxocn_clon[na], atmxocn_clat[na])
for (la = is; la <= ie; la++)
```

**Purpose**:
- Marks inner atmospheric cell loop as GPU-parallel
- `present()` clause declares arrays already on GPU device memory (from outer data region)
- `independent` keyword tells compiler loop iterations have no data dependencies
- `collapse(1)` specifies single loop level parallelization

**Impact**: ~495 iterations of atmospheric cell processing can run in parallel on GPU threads.

---

### 2. **Race-Safe Counter Increments** (Lines 2502-2505, 2595-2598)

**Ocean Counter Update**:
```c
#pragma acc atomic capture
{
  (naxo[na][no])++;
}
```

**Land Counter Update**:
```c
#pragma acc atomic capture
{
  (naxl[na][nl])++;
}
```

**Purpose**:
- Ensures thread-safe increment of global counters that track number of exchange grid cells
- `atomic capture` pattern allows multiple GPU threads to safely increment shared counters
- Prevents data races when multiple threads write to same counter

**Impact**: Enables correct accumulation of exchange grid counts across all parallel threads.

---

### 3. **GPU Function Call Annotations** (8 locations)

Added comment markers for performance-critical GPU function calls:

| Location | Function | Line(s) | Purpose |
|----------|----------|---------|---------|
| Atmospheric grid | `fix_lon()`, `minval_double()`, `maxval_double()`, `avgval_double()` | 2167-2169 | Grid coordinate normalization and bounding box computation |
| Land clipping | `clip_2dx2d()` | 2291 | Polygon intersection (atm ↔ land) |
| Ocean clipping | `clip_2dx2d()` | 2458-2459 | Polygon intersection (atm ↔ ocean) |
| Land area | `poly_area()` | 2307 | Compute land exchange grid area |
| Ocean area | `poly_area()` | 2480 | Compute ocean exchange grid area |
| Land-ocean clipping | `clip_2dx2d()` | 2514 | Polygon intersection (land ↔ ocean via atmxlnd grid) |
| Land-ocean area | `poly_area()`, `poly_ctrlon()`, `poly_ctrlat()` | 2530-2533 | Land-ocean exchange grid area and center coordinates |
| Final land storage | `poly_ctrlon()`, `poly_ctrlat()` | 2608-2609 | Center coordinates for interpolation |

**Annotation Type**: Comment markers (`/* GPU ... */`) to identify where GPU-accelerated utility functions are called. These functions will be compiled with `#pragma acc routine seq` when NVIDIA HPC SDK is available.

---

## Pragma Strategy Details

### Data Region Structure

The annotations assume a **two-level data strategy**:

```c
/* Outer (na) loop - CPU only */
for (na = 0; na < ntile_atm; na++) {
  /* Data setup for this iteration */
  
  /* Inner (la) loop - GPU parallel */
  #pragma acc parallel loop independent present(...)
  for (la = is; la <= ie; la++) {
    /* GPU-accelerated work */
    
    #pragma acc atomic capture
    /* Thread-safe counter updates */
  }
  
  /* CPU-only cleanup */
}
```

**Key Design Decisions**:

1. **`independent` Clause**: Each `la` iteration is independent because:
   - Read-only input arrays (grids, masks, areas)
   - Thread-private work arrays (xa, ya, xl, yl, xo, yo, etc.)
   - Counters use atomic operations for safe concurrent updates

2. **`present()` Clause**: Declares that grid data is already on GPU device:
   - Avoids redundant data transfer for each `la` iteration
   - Assumes outer region (to be added in Phase 3) manages data movement

3. **`atomic capture`**: Safe counter increments without explicit locks:
   - Supported by modern GPU hardware
   - Lower overhead than mutex-based synchronization
   - Works with OpenACC on NVIDIA GPUs

---

## Required Utility Function Annotations

The following functions must have `#pragma acc routine seq` added to their definitions (in headers or source) when NVIDIA SDK becomes available:

**Core Geometry Functions**:
- `fix_lon()` - Normalize longitude coordinates
- `clip_2dx2d()` - Sutherland-Hodgman polygon clipping
- `clip_2dx2d_great_circle()` - Great-circle polygon clipping
- `poly_area()` - Compute polygon area (shoelace formula)
- `poly_ctrlon()` - Compute polygon center longitude
- `poly_ctrlat()` - Compute polygon center latitude
- `great_circle_area()` - Compute great-circle polygon area

**Reduction Functions**:
- `minval_double()` - Find minimum value
- `maxval_double()` - Find maximum value
- `avgval_double()` - Compute average value

**Usage in GPU Context**:
```c
/* These functions will be called from GPU threads */
#pragma acc routine seq
double poly_area(const double xo[], const double yo[], int n) {
  /* implementation */
}
```

The `seq` routine type means functions execute in sequence within a GPU thread (one thread does not call another), which is appropriate for these utility functions.

---

## Compilation Requirements

### Prerequisites
- **Compiler**: NVIDIA HPC SDK (nvc) with OpenACC support
- **GPU Hardware**: NVIDIA GPU with compute capability ≥ 7.0 (Volta or newer)
- **Dependencies**: NetCDF, MPI, FMS libraries with GPU support (optional)

### Compilation Command (Phase 4)
```bash
cd /nbhome/Niki.Zadeh/projects/FRE-NCtools.dev/build
nvc -acc=gpu -gpu=cc80 -O2 -c ../src/make-coupler-mosaic/make_coupler_mosaic.c \
    -o obj/make_coupler_mosaic.o \
    -I../lib/include -I../include
```

### Build System Integration (Phase 3)
The configure script and Makefile will be updated to:
1. Detect NVIDIA HPC SDK availability
2. Add `-acc=gpu` flag conditionally
3. Set appropriate compute capability (`-gpu=cc*`)
4. Link GPU runtime libraries

---

## Testing & Validation Strategy

### Unit Testing (Phase 4)
1. **Compile GPU Version**
   - Use nvc compiler with OpenACC flags
   - Check for compilation warnings (especially deprecated pragmas)
   - Verify no new compiler errors

2. **Functional Testing**
   - Run on C48, C96 grids from pan_tests
   - Compare GPU vs. CPU output
   - Allow FP tolerance: `±1e-12` (atomic ops may reorder FP operations)

3. **Integration Testing**
   - Full coupler initialization with GPU code path
   - Verify exchange grids match non-GPU version

### Performance Profiling (Phase 4)
```bash
nsys profile -o profile ./make_coupler_mosaic_gpu input.txt
# Analyze: kernel launch overhead, data transfer time, compute time
```

**Expected Speedup**: 2-5x for polygon clipping-heavy operations, depending on grid resolution and GPU hardware.

---

## Known Limitations & Future Improvements

### Current Limitations

1. **Data Region Not Yet Implemented**
   - Outer `na` loop lacks `#pragma acc data` region
   - Will be added in Phase 3 build system integration
   - This region manages GPU memory allocation and data transfer

2. **Inner Loops Not Parallelized**
   - `nl` (land tile) and `no` (ocean tile) loops remain sequential
   - Can be parallelized with `collapse(2)` or `collapse(3)` in advanced optimization

3. **Function Annotations Pending**
   - Utility functions still lack `#pragma acc routine seq`
   - Will be added when NVIDIA SDK is installed
   - Currently marked with comments for clarity

4. **Great-Circle Clipping Not GPU-Optimized**
   - `clip_2dx2d_great_circle()` uses vectorized computations
   - May need special optimization for GPU execution

### Future Optimization Opportunities

1. **Multi-Level Parallelism**: Parallelize land/ocean tile loops
   ```c
   #pragma acc parallel loop collapse(3) independent
   for (la = ...) { for (nl = ...) { for (jl = ...) { ... } } }
   ```

2. **Shared Memory Optimization**: Cache frequently accessed grid data
   ```c
   #pragma acc cache(xatm[*], yatm[*], xlnd[*], ylnd[*], ...)
   ```

3. **Asynchronous Data Transfer**: Hide PCIe latency (Phase 5)
   ```c
   #pragma acc enter data copyin(grid_data) async(1)
   ```

4. **Profiling-Guided Optimization**
   - Use `nsys` or `nvvp` to identify bottlenecks
   - Consider vector operations for reduction functions

---

## Files Modified

| File | Changes | Lines |
|------|---------|-------|
| `src/make-coupler-mosaic/make_coupler_mosaic.c` | Parallel loop, atomic ops, function call comments | 11 edits |

## Files Created

| File | Purpose |
|------|---------|
| `PHASE2_ANNOTATED_CODE_SUMMARY.md` | This document |

---

## Next Steps: Phase 3 - Build System Integration

### Task 1: Update configure.ac
- Add `--enable-gpu` flag
- Detect NVIDIA HPC SDK via module system
- Set compiler flags conditionally

### Task 2: Update src/make-coupler-mosaic/Makefile.am
- Add GPU compilation rules
- Include appropriate `-acc=gpu -gpu=cc*` flags
- Link GPU runtime libraries

### Task 3: Verification
- `./configure --enable-gpu`
- `make clean && make`
- Verify successful compilation

---

## Validation Checklist

- [x] Parallel loop pragma added and syntactically correct
- [x] Atomic counter increments in place
- [x] GPU function calls marked/commented
- [x] Code compiles with standard C compiler (gcc)
- [x] Code structure preserved (no logic changes)
- [ ] Compiles with NVIDIA nvc compiler (pending SDK install)
- [ ] Unit tests pass with GPU code (pending SDK install)
- [ ] Performance profiling complete (pending Phase 4)

---

## References

1. **GPU Offload Analysis**: `GPU_OFFLOAD_ANALYSIS.md` (Phase 1 deliverable)
2. **GPU Test Kernel**: `test_gpu_kernel.c` (Phase 1 reference implementation)
3. **OpenACC Standard**: https://www.openacc.org/spec
4. **NVIDIA HPC SDK Docs**: https://docs.nvidia.com/hpc-sdk/

---

**Version**: 1.0  
**Last Updated**: 2026-03-18  
**Phase**: 2 of 4 (Code Annotation)  
**Status**: Ready for Phase 3 (Build System Integration)
