# Controlling Arbitrary Internet Queues with Titrate

This repository contains the source code and instructions for artifact evaluation for our NSDI'26 paper Controlling Arbitrary Internet Queues with Titrate.

TODO: maybe add description and roadmap for evaluation here
TODO: add badge selection?
TODO: explain why not the testbed stuff


## Getting Started Instructions

Setup:
```
cd ns-3.34
./waf clean
./waf distclean
CXXFLAGS="-std=c++17" ./waf configure --build-profile=optimized --disable-examples --disable-tests --disable-python
./waf
```

Validate whether experiment runs:
```
cd ns-3.34
python3 ../run_ns3.py --conf ../getting_started/run.conf
```
This launches one simple experiment, which should finish in about two minutes. You should expect to see three files under `~/Titrate/ns-3.34/logs/simple_appconf_paw/1000000_111_0_204_12_1000000/200_12500000/0/1_500_2_4/5_10_50_3_3_3_5_10_3_5_1_10_5/500_100_0/1/`: `flowmonitor.xml`,`output.tr`,`tor.tr`. The expected `output.tr` can be found in `~/Titrate/getting_started/expected_output.tr`.


## Detailed Instructions

We will show how to reproduce results for the following figures in the paper and list the corresponding directories. Note that these experiments use 1Gbps links and take multiple days to finish. As such, I have also included the expected results that you will obtain if you really run the experiments for this long. My plotting scripts run based on these numbers. If you actually run for multiple days and want to plot based on your numbers, you can simply change `TODO: add` and plot accordingly.
| Figure | Directory | Subsection |
|----------|----------|----------|
| Figures 8 & 9   | thptlat/   | Throughput & Latency  |
| Figure 10   | burst/   | Burst |
| Figure 11   | burst/   | Burst |
| Figure 12   | burst/   | Burst |
| Figure 13   | burst/   | Burst |
| Figure 14   | burst/   | Burst |


### Throughput & Latency

Claim: TODO: added figure caption here; is it fine? hopefully this claim could cover what the Internet testbed has to say, then we can argue that is not key
This corresponds to Figure 8 & 9 in the paper. 
TODO: add how many experiments in total

Run experiments -- may need at least TODO:add number GB space to store data, take days, PIE takes 10+ days:
```
cd ns-3.34/
python3 ../run_ns3.py --conf ../detailed_ae/thptlat/run.conf 
# 240 experiments in total, can add --worker
python3 ../run_ns3.py --conf ../detailed_ae/thptlat/quickrun.conf 
```

Compile your data (if you didn't wait until experiments finish, do the next bulk instead)
The expected data are in `expected_data_throughput.txt` and `expected_data_latency.txt`
```
cd detailed_ae/thptlat
python3 process_data.py
```
```
cp expected_data_throughput.txt data_throughput.txt
cp expected_data_latency.txt data_latency.txt
```


Plot:
```
python3 plot_fig8.py
python3 plot_fig9.py
```
The expected plots are `expected_fig8.pdf` and `expected_fig9.pdf`.


### Burst


## Acknowledgements

Zili Meng, Vamsi



## Configure NS-3
```
CXXFLAGS="-std=c++17" LDFLAGS="-lboost_filesystem -lboost_system" ./waf configure
```
## Process Logs
```

```

------
## What Ann did to get the first experiment running

### Set up NS-3
```
cd ns-3.34
./waf configure
./waf clean
./waf distclean
CXXFLAGS="-std=c++17" LDFLAGS="-lboost_filesystem -lboost_system" ./waf configure
./waf
```
### Set up experiment
```
cd ns-3.34
mkdir traces
cd ..
python3 maxwell_tracegen.py --rate 30.0 --cov 0.01
cd ns-3.34
python3 ../run_ns3.py --conf ../eva1-tradeoff.conf
cd logs
```

### What Ann did to get the program compile on netsyn-01 server
```
cd ns-3.34
./waf configure
./waf clean
./waf distclean
CXXFLAGS="-std=c++17" ./waf configure --build-profile=optimized --disable-examples --disable-tests --disable-python
./waf
```

------
## Some notes post-nsdi25sp 

### How to run experiments
```
cd ~/Buffer/ns-3.34/
python3 ../run_ns3.py --conf ../experiments/reunderstand_nsdi25sp/postnsdi25sp_test.conf 
# when generation, the conf file includes a bunch of commands starting with star-buffer-mp
```

### Revelant codes
- `~/Buffer/ns-3.34/scratch/star-buffer-mp.cc`
    - `Statxx()`,`InstallApp()`,`InvokeToRStats()`
    - `main()`:
    ```
    conf
    sharedMemory
    set up input ports
        for sink in [0,numSinks):
            for sender in sendersNodesArray[sink]:
    set up output ports
    set up apps
    ```
- `~/Buffer/ns-3.34/src/traffic-control/model/shared-memory.cc`
    - `setUp()`,`allocateBufferSpaceSimple()`
    - some auxiliary functions
- `~/Buffer/ns-3.34/src/traffic-control/model/gen-queue-disc.cc`
    - `MyBM()`,`DoEnqueue()`,`startProbing()`
    - `probeMinMonitorLongCollectSimple()`,`probeMinMonitorLongInvoke()`
    - some auxiliary functions