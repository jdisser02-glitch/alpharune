# Post-ban matchup matrix m0

1000 games per pair (both directions pooled), from `lab/results/*-meta-m0-row-*.json`; made by `lab/tools/meta_matrix.py solve`. Rates are what the lab's pilots do, not what players do: a legend whose pilot trained less looks weaker than it is.

| list | pilot | against the prior field | share at equilibrium (10%-90% of 200 redraws) | prior share |
|---|---|---|---|---|
| Volibear (lab's post-ban list) | spec_volibear_132_v0 | 90.1% | 4% (1-6) | 4% |
| Volibear (Ritualo + 2 Rengar) | spec_volibear_132_v0 | 87.4% | 96% (94-98) | 0% |
| Mordekaiser (Neal) | spec_mordekaiser_070_v0 | 77.2% | 0% (0-0) | 4% |
| Riven, Deserter (post-ban frame) | spec_riven_120_v0 | 67.2% | 0% (0-0) | 10% |
| Evelynn (Dunc) | spec_evelynn_018_v0 | 65.7% | 0% (0-0) | 0% |
| Master Yi (PCQ, post-ban) | net_164_v3 | 62.0% | 0% (0-0) | 8% |
| Evelynn (Jeremy, 13 Nov) | spec_evelynn_018_v0 | 56.4% | 0% (0-0) | 4% |
| Riven, Shattered (post-ban frame) | spec_riven_120_v0 | 55.7% | 0% (0-0) | 10% |
| Kai'Sa (Jason Tian, post-ban) | spec_kai_sa_025_v1 | 52.8% | 0% (0-0) | 26% |
| Orianna (luoluoluofu, post-ban) | spec_orianna_054_v1 | 41.8% | 0% (0-0) | 2% |
| Rek'Sai (Rajan Nandan) | net_164_v3 | 38.9% | 0% (0-0) | 5% |
| Akali (Vincent Nguyen, post-ban) | spec_akali_008_v3 | 34.8% | 0% (0-0) | 4% |
| Kennen (zombro, post-ban) | spec_kennen_020_v0 | 24.4% | 0% (0-0) | 22% |

Row beats column (%):

| | volibear | volibear_ritualo | mordekaiser | riven_deserter | evelynn_dunc | master_yi | evelynn | riven_shattered | kai_sa | orianna | rek_sai | akali | kennen |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| volibear | - | 45 | 73 | 83 | 65 | 97 | 78 | 87 | 91 | 96 | 98 | 99 | 100 |
| volibear_ritualo | 55 | - | 74 | 76 | 60 | 96 | 67 | 81 | 88 | 90 | 98 | 98 | 100 |
| mordekaiser | 27 | 26 | - | 57 | 56 | 78 | 65 | 68 | 83 | 42 | 89 | 97 | 96 |
| riven_deserter | 17 | 24 | 43 | - | 59 | 53 | 66 | 64 | 69 | 73 | 84 | 84 | 86 |
| evelynn_dunc | 35 | 40 | 44 | 41 | - | 72 | 61 | 55 | 48 | 72 | 87 | 96 | 99 |
| master_yi | 3 | 4 | 22 | 47 | 28 | - | 46 | 60 | 56 | 64 | 76 | 72 | 96 |
| evelynn | 22 | 33 | 35 | 34 | 39 | 54 | - | 43 | 38 | 65 | 73 | 88 | 96 |
| riven_shattered | 13 | 19 | 32 | 36 | 45 | 40 | 57 | - | 50 | 68 | 77 | 74 | 82 |
| kai_sa | 9 | 12 | 17 | 31 | 52 | 44 | 62 | 50 | - | 77 | 58 | 74 | 77 |
| orianna | 4 | 10 | 58 | 27 | 28 | 36 | 35 | 32 | 23 | - | 68 | 61 | 72 |
| rek_sai | 2 | 2 | 11 | 16 | 13 | 24 | 27 | 23 | 42 | 32 | - | 47 | 69 |
| akali | 1 | 2 | 3 | 16 | 4 | 28 | 12 | 26 | 26 | 39 | 53 | - | 69 |
| kennen | 0 | 0 | 4 | 14 | 1 | 4 | 4 | 18 | 23 | 28 | 31 | 31 | - |
