| Command | Mean [ms] | Min [ms] | Max [ms] | Relative |
|:---|---:|---:|---:|---:|
| `env OMP_NUM_THREADS=1 ./build-release/app/bz --cache-age 999999999 --no-color scan` | 13.0 ± 0.1 | 12.8 | 13.3 | 1.03 ± 0.07 |
| `env OMP_NUM_THREADS=8 ./build-release/app/bz --cache-age 999999999 --no-color scan` | 12.6 ± 0.8 | 11.4 | 15.1 | 1.00 |
