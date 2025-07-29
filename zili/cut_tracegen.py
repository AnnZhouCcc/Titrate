import argparse
import itertools
import os


if __name__ == '__main__':
    parser = argparse.ArgumentParser ()
    parser.add_argument ('--ratio', type=int, help="ratio of abw cut")
    parser.add_argument ('--time', type=int, help="time of abw cut")
    parser.add_argument ('--initbw', type=int, default=30, help="initial bw")
    parser.add_argument ('--tracedir', type=str, default='./ns-3.34/traces/', help="trace dir")
    args = parser.parse_args ()

    totalLen = 400
    ratios = [2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 25, 30, 35, 40]
    times = list (range (150, 250, 10))
    if args.ratio:
        ratios = [args.ratio]
    if args.time:
        times = [args.time]
    for ratio, time in itertools.product (ratios, times):
        with open (os.path.join (args.tracedir, 'cut-%d-%d.pitree-trace' % (ratio, time)), 'w') as f:
            for i in range (totalLen):
                if i < time:
                    f.write ('%.2fMbps 0ms 0.00\n' % args.initbw)
                else:
                    f.write ('%.2fMbps 0ms 0.00\n' % (args.initbw / ratio))
