| Command | Mean [ms] | Min [ms] | Max [ms] | Relative |
|:---|---:|---:|---:|---:|
| `OMP_NUM_THREADS=1 ./build/app/bz --cache-age 999999 --no-color item ENCHANTED_COOKIE` | 170.9 ± 5.3 | 166.3 | 182.2 | 1.99 ± 0.08 |
| `OMP_NUM_THREADS=8 ./build/app/bz --cache-age 999999 --no-color item ENCHANTED_COOKIE` | 85.7 ± 2.1 | 82.9 | 89.7 | 1.00 |
