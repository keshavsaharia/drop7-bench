#!/bin/bash
cd /home/keshav/Developer/drop7-bench
export RUN_ID=RUN-20260903T190433Z-a87fd7fc THREADS=32
export EXPERIMENT_ID=EX-20260903-nnue-evolution-continuation2-d3-80eebad3
export LEASE_START=0xa52ea100 SCREEN_START=0xa52f2100
export SCREEN_LEASE=research/seeds/leases/SL-20260903T190000Z-a52f2100.json
export GENERATIONS=1000 EVOLVE_SEED=0x0e701e5a
export INIT=/home/keshav/Developer/drop7-bench/artifacts/results/EX-20260902-nnue-evolution-d3-v2-49c18bc2/RUN-20260902T035644Z-c1fd8987/pretrain/init.bin
export RESUME_POP=/home/keshav/Developer/drop7-bench/runs/RUN-20260903T032832Z-a76a6cf7/nnue-evolution/evolve/population-150.bin
export BASELINE=/home/keshav/Developer/drop7-bench/artifacts/results/EX-20260903-nnue-evolution-continuation-d3-f8ce9181/RUN-20260903T032832Z-a76a6cf7/evolve/candidate-weights.bin
export SIGMA_REL=0.05 SIGMA_TAU=1500 SIGMA_FLOOR=0.01
export PLATEAU_WINDOW=100 PLATEAU_EVERY=50 PLATEAU_MIN=100
export EVOLVE_WALL=259200
exec approaches/lifetime-objective/nnue-evolution/scripts/pipeline.sh chain-evolve
