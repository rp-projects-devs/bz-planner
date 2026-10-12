| Command | Mean [ms] | Min [ms] | Max [ms] | Relative |
|:---|---:|---:|---:|---:|
| `env OMP_NUM_THREADS=1 ./build/app/bz --no-color --cache-age 99999999 scan --slots 21 --budget 200m --liquidity 10` | 464.1 ± 1.3 | 462.7 | 466.2 | 35.63 ± 0.43 |
| `env OMP_NUM_THREADS=8 ./build/app/bz --no-color --cache-age 99999999 scan --slots 21 --budget 200m --liquidity 10` | 93.7 ± 4.1 | 90.6 | 104.8 | 7.19 ± 0.33 |
| `env OMP_NUM_THREADS=1 ./build/app/bz --no-color --cache-age 99999999 scan` | 13.0 ± 0.2 | 12.9 | 13.4 | 1.00 |
| `env OMP_NUM_THREADS=8 ./build/app/bz --no-color --cache-age 99999999 scan` | 13.4 ± 0.9 | 12.9 | 15.2 | 1.03 ± 0.07 |
