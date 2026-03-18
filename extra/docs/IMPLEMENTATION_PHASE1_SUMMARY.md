# GPU Offloading Implementation Plan - Phase 1 Complete

**Document**: Implementation roadmap for offloading the `compute_atm_lnd_ocn_exchange()` inner loop to GPU  
**Date**: 2026-03-18  
**Status**: Phase 1 (Analysis) ✓ Complete | Phase 2-4 (Implementation) Ready

---

## Phase 1: Analysis & Planning ✓ COMPLETE

### Deliverable 1: Comprehensive Analysis Document
**File**: [`GPU_OFFLOAD_ANALYSIS.md`](./GPU_OFFLOAD_ANALYSIS.md)

**Contents**:
- ✓ Loop structure hierarchy (lines 1946–2590)
- ✓ Data dependency matrix (read-only, thread-private, race conditions)
- ✓ Detailed GPU implementation strategies (Option A & B)
- ✓ Function compatibility checklist
- ✓ Data movement strategy with OpenACC pragmas  
- ✓ Known challenges and mitigation techniques

**Key Findings**:
| Category | Count | Note |
|----------|-------|------|
| Read-only grids | 9 | {x,y,z}atm, {x,y,z}lnd, {x,y,z}ocn, areas |
| Thread-private arrays | 12 | Work buffers for polygon clipping |
| Race-condition variables | 4 | naxo, naxl, atmxocn_*, atmxlnd_* |
| GPU-compatible functions | 8 | All marked `#pragma acc routine seq` |

---

### Deliverable 2: GPU Test Kernel
**File**: [`test_gpu_kernel.c`](./test_gpu_kernel.c)

**Features**:
- ✓ Minimal but complete GPU kernel (`gpu_atm_lnd_kernel()`)
- ✓ Full OpenACC data movement (`#pragma acc data`, `copyin`, `copy`)
- ✓ Nested loop parallelization with `#pragma acc parallel loop`
- ✓ Atomic operation pattern for race safety (`#pragma acc atomic capture`)
- ✓ 8 GPU-compatible mathematical functions with `#pragma acc routine seq`
- ✓ Test harness with synthetic grid data (6×6 cells)

**Compile & Run**:
```bash
# With NVIDIA HPC SDK installed:
nvc -acc=gpu -gpu=cc80 -O2 test_gpu_kernel.c -lm -o test_gpu_kernel
./test_gpu_kernel
```

**Expected Output**:
```
=== GPU Offloading Kernel Test ===
Grid setup complete:
  ATM grid: 6x6
  LND grid: 6x6
Kernel executed in 0.XX seconds
=== Exchange Grid Results ===
naxl[0][0] = N (number of atm-lnd exchange cells)
=== Test PASSED ===
```

---

## Phase 2: OpenACC Annotation (Ready to Execute)

### Tasks
1. **Instrument main loop** (lines 1946–2590):
   - Add `#pragma acc data` region wrapping outer `na` iteration
   - Annotate inner `la` loop with `#pragma acc parallel loop independent`
   - Mark all utility functions with `#pragma acc routine seq`

2. **Handle race conditions**:
   - Use `#pragma acc atomic capture` for `naxo[na][no]++` and `naxl[na][nl]++`
   - Ensure memory-safe writes to exchange grid arrays

3. **Data management**:
   - Copyin: Grid data (xatm, yatm, xlnd, etc.) 
   - Copy: Exchange grid arrays and counters (naxo, atmxocn_area, etc.)
   - Copyout: Results after kernel completes

### Expected Code Changes
- **Lines affected**: ~150 lines (pragmas + minimal restructuring)
- **Compilation**: Should succeed with NVIDIA HPC compiler
- **Expected speedup**: 2–10× on V100/A100 depending on grid size

---

## Phase 3: Build System Integration (Ready)

### Changes Required
1. **`configure.ac`**:
   - Add `--enable-gpu` flag
   - Detect NVIDIA HPC SDK (`nvc`, `pgc`)
   - Add GPU compiler flags conditional block

2. **`src/make-coupler-mosaic/Makefile.am`**:
   - Add `-acc=gpu -gpu=cc80 -O2` flags if GPU enabled

3. **Build Documentation**:
   - Update README.md with GPU build instructions
   - Document NVIDIA HPC SDK requirements

---

## Phase 4: Testing & Validation (Ready)

### Unit Testing
- Compile test harness in current environment
- Verify atomic operations work correctly
- Compare GPU vs. CPU numerical output

### Integration Testing  
- Build full application with `--enable-gpu`
- Run on pan_tests dataset (small grids: C48, C96)
- Verify output files match CPU-only version

### Performance Benchmarking
- Profile with `nsys` or `pgprof`
- Measure time per `na` iteration on GPU vs. CPU
- Calculate achieved memory bandwidth, FLOPS

### Robustness
- Test on multiple GPU architectures (Volta, Ampere, Hopper)
- Verify fallback to CPU if GPU unavailable
- Check for out-of-memory conditions

---

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| GPU Technology | OpenACC | Portable, good for scientific code, compiler-driven |  
| Loop Scope | Inner `la` loop | Fine-grained parallelism, minimal data movement |
| Race Handling | Atomic increment (Option A) | Simpler, acceptable for this use case |
| Compiler | NVIDIA HPC SDK | Full OpenACC/CUDA support, best performance |
| GPU Target | CC 8.0+ (Ampere) | Modern GPU with good atomic ops |

---

## Known Challenges & Mitigations

| Challenge | Mitigation |
|-----------|-----------|
| printf() on GPU | Use `#pragma acc routine host` wrappers or skip |
| Large arrays exceed GPU memory | Use unified memory or keep on host |
| Function inlining | Manual inlining of hot functions if needed |
| FP precision variation | Allow small tolerance (1e-12) in correctness tests |
| Atomic operation bottleneck | Profile; fallback to local accumulation if needed |

---

## Timeline Estimate

| Phase | Task | Effort | Blocker |
|-------|------|--------|---------|
| 2 | Annotate loops | 2–3 hours | None |
| 2 | Verify compilation | 1 hour | NVIDIA SDK |
| 3 | Integrate with build | 1–2 hours | None |
| 4 | Unit test | 2 hours | Test data |
| 4 | Full integration | 3–4 hours | None |
| 4 | Performance tune | 4–8 hours | Profiler results |
| **Total** | — | **13–20 hours** | NVIDIA SDK required |

---

## Next Steps

### Immediate (Phase 2)
1. Verify NVIDIA HPC SDK availability in deployment environment
2. Begin code annotation using test kernel as template
3. Compile and debug with sample grids (C48)

### Short-term (Phase 3-4)
4. Integrate GPU flags into build system
5. Run full tests on pan_tests dataset
6. Benchmark and document performance gains

### Optional Future Work
- Implement Option B (thread-local accumulation) if atomic ops are bottleneck
- Add GPU memory management for very large grids (MAXXGRID >> 1e6)
- Port other compute-intensive functions (clip_2dx2d, great_circle_area)
- Support additional architectures (AMD MI300X via HIP)

---

## References

**OpenACC Standards**:
- [OpenACC 3.2 Specification](https://www.openacc.org/)
- [NVIDIA HPC SDK Documentation](https://docs.nvidia.com/hpc-sdk/)

**Tools**:
- [NVIDIA Nsight Systems Profiler](https://docs.nvidia.com/nsight-systems/)
- [NVIDIA PGProf Performance Tool](https://docs.nvidia.com/hpc-sdk/)

**Related Code**:
- Existing GPU utilities: `lib/libfrencutils_gpu/`  
- Host utility functions: `lib/libfrencutils/create_xgrid.h`

---

**Document Author**: GPU Offloading Task Force  
**Last Updated**: March 18, 2026  
**Status**: Ready for Phase 2 Implementation
