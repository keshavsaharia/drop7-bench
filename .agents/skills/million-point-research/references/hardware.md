# Hardware-aware research

Read `docs/hardware/amd-ryzen-halo.md` for the current AMD/ROCm plan and official
compatibility links. Detect capabilities; do not infer them from the machine's
marketing name.

1. Run the machine doctor and retain its JSON with every performance result.
2. Benchmark CPU thread scaling before choosing a thread count. Separate game-
   level workers from decision/search threads and prevent nested oversubscription.
3. Leave explicit memory headroom. On an integrated GPU, CPU and GPU allocations
   share physical RAM and memory bandwidth.
4. Keep exact simulation and irregular tree traversal CPU-first until an end-
   to-end GPU prototype passes transition/RNG parity and beats the CPU baseline.
5. Use the GPU first for dense neural training and batched leaf or sibling
   inference. Test realistic batches and complete-pipeline throughput.
6. Treat mixed precision, changed reduction order, chance samples, completed
   depth, or selected actions as an algorithmic change unless equivalence is
   proved.
7. Record kernel, driver, ROCm/HIP, framework, device target, affinity, power
   state, peak shared memory, temperature, and profiler availability.

Kernel, BIOS, firmware, TTM/GTT, and system-wide driver changes require explicit
operator approval. A research directive does not grant it.
