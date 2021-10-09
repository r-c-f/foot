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


| Benchmark (times in ms)       | Foot (GCC+PGO) 1.9.2 | Foot 1.9.2 | Alacritty 0.9.0 | URxvt 9.26 | XTerm 369 |
|-------------------------------|---------------------:|-----------:|----------------:|-----------:|----------:|
| cursor motion                 |                13.50 |      16.32 |           27.10 |      23.46 |   1415.38 |
| dense cells                   |                38.77 |      53.13 |           89.36 |    2007.00 |   2126.60 |
| light cells                   |                 7.73 |       8.72 |           20.35 |      21.06 |    113.34 |
| scrollling                    |               150.27 |     153.76 |          145.07 |     139.23 |  10088.00 |
| scrolling bottom region       |               144.88 |     148.44 |          129.13 |     156.86 |  10166.00 |
| scrolling bottom small region |               142.45 |     137.81 |          167.63 |     183.35 |   9831.50 |
| scrolling fullscreen          |                11.23 |      11.91 |           20.12 |      21.21 |    290.80 |
| scrolling top region          |               143.80 |     147.37 |          148.63 |     489.57 |  10029.00 |
| scrolling top small region    |               139.76 |     144.37 |          165.97 |     308.76 |   9877.00 |
| unicode                       |                21.94 |      21.50 |           27.72 |    1344.88 |   7402.00 |
