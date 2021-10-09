# Benchmarks

## vtebench

All benchmarks are done using [vtebench](https://github.com/alacritty/vtebench):

```sh
./target/release/vtebench -b ./benchmarks --dat /tmp/<terminal>
```

## 2021-06-25

### System

CPU: i9-9900

RAM: 64GB

Graphics: Radeon RX 5500XT


### Terminal configuration

Geometry: 2040x1884

Font: Fantasque Sans Mono 10.00pt/23px

Scrollback: 10000 lines


### Results

| Benchmark (times in ms)       | Foot (GCC+PGO) 1.9.2 | Foot 1.9.2 | Alacritty 0.9.0 | URxvt 9.26 | XTerm 369 |
|-------------------------------|---------------------:|-----------:|----------------:|-----------:|----------:|
| cursor motion                 |                13.69 |      15.63 |           29.16 |      23.69 |   1341.75 |
| dense cells                   |                40.77 |      50.76 |           92.39 |   13912.00 |   1959.00 |
| light cells                   |                 5.41 |       6.49 |           12.25 |      16.14 |     66.21 |
| scrollling                    |               125.43 |     133.00 |          110.90 |      98.29 |   4010.67 |
| scrolling bottom region       |               111.90 |     103.95 |          106.35 |     103.65 |   3787.00 |
| scrolling bottom small region |               120.93 |     112.48 |          129.61 |     137.21 |   3796.67 |
| scrolling fullscreen          |                 5.42 |       5.67 |           11.52 |      12.00 |    124.33 |
| scrolling top region          |               110.66 |     107.61 |          100.52 |     340.90 |   3835.33 |
| scrolling top small region    |               120.48 |     111.66 |          129.62 |     213.72 |   3805.33 |
| unicode                       |                10.19 |      11.27 |           14.72 |     787.77 |   4741.00 |


## 2021-03-20

### System

CPU: i5-8250U

RAM: 8GB

Graphics: Intel UHD Graphics 620


### Terminal configuration

Geometry: 945x1020

Font: Dina:pixelsize=12

Scrollback=10000 lines


### Results


| Benchmark (times in ms)       | Foot (GCC+PGO) 1.8.0 | Foot 1.8.0 | Alacritty 0.8.0 | URxvt 9.26 | XTerm 368 |
|-------------------------------|---------------------:|-----------:|----------------:|-----------:|----------:|
| cursor motion                 |                14.49 |      16.60 |           26.89 |      23.45 |   1303.38 |
| dense cells                   |                41.00 |      52.45 |           92.02 |    1486.57 |  11957.00 |
| light cells                   |                 7.97 |       8.54 |           21.43 |      20.45 |    111.96 |
| scrollling                    |               158.85 |     158.90 |          148.06 |     138.98 |  10083.00 |
| scrolling bottom region       |               153.83 |     151.38 |          142.13 |     151.30 |   9988.50 |
| scrolling bottom small region |               143.51 |     141.46 |          162.03 |     192.37 |   9938.00 |
| scrolling fullscreen          |                11.56 |      11.75 |           22.96 |      21.49 |    295.40 |
| scrolling top region          |               148.96 |     148.18 |          155.05 |     482.05 |  10036.00 |
| scrolling top small region    |               144.26 |     149.76 |          159.40 |     321.69 |   9942.50 |
| unicode                       |                21.02 |      22.09 |           25.79 |   14959.00 |  88697.00 |
