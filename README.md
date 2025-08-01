# Controlling Arbitrary Internet Queues with Titrate

This repository contains the source code and instructions for artifact evaluation for our NSDI'26 paper Controlling Arbitrary Internet Queues with Titrate. The following codes are run with `g++ 11.4.0` and `Python 3.10.12`.

We are submitting this artifact for all three badges: Artifact Available, Artifact Functional, and Results Reproduced.

## Getting Started Instructions

Set up ns3:

```
cd ns-3.34
./waf clean
./waf distclean
CXXFLAGS="-std=c++17" ./waf configure --build-profile=optimized --disable-examples --disable-tests --disable-python
./waf
```

Set up dependencies:

```
pip3 install -r requirements.txt
```

Validate whether experiment runs:

```
cd ns-3.34
python3 ../run_ns3.py --conf ../getting_started/run.conf
```

This launches one simple experiment, which should finish in about two minutes. You should expect to see three files under `logs/simple_appconf_paw/1000000_111_0_204_12_1000000/200_12500000/0/1_500_2_4/5_10_50_3_3_3_5_10_3_5_1_10_5/500_100_0/1/`: `flowmonitor.xml`,`output.tr`,`tor.tr`. The expected `output.tr` can be found in `~/Titrate/getting_started/expected_output.tr`.

## Detailed Instructions

There are six main experiments that showcase the key results and major claims of our work. The table below summarizes the six experiments -- which figures they refer to, which directories contain the codes and which subsections describe the experiment details.

| Figure            | Directory     | Subsection                                                              |
| ----------------- | ------------- | ----------------------------------------------------------------------- |
| Figures 8 & 9     | thptlat/      | [Throughput & Queuing Latency](#throughput--queuing-latency)            |
| Figure 10         | burst/        | [Burst Completion Time](#burst-completion-time)                         |
| Figure 11         | multirtt/     | [Multiple RTTs on Multiple Ports](#multiple-rtts-on-multiple-ports)     |
| Figure 12         | multiburst/   | [Multiple Bursts on Multiple Ports](#multiple-bursts-on-multiple-ports) |
| Figures 13a & 13b | deltabw/      | [Varying Bandwidth](#varying-bandwidth)                                 |
| Figures 14a & 14b | deltatraffic/ | [Varying Traffic](#varying-traffic)                                     |
| Figures 15b & 15c | testbed/      | [Testbed](#testbed)                                                     |

Quick run: In many cases, a single experiment runs for 5+ days and takes up 20+GB of memory. A single experiment can only be run on one core and parallism only happens across multiple experiments. As such, we will also provide a `Quick Run` option, where it runs a shorter version of the experiments to make sure experiments are functional and continues the figure plotting with data files provided to make sure results are correct.

Figures not included: We have excluded some figures from the paper that we deem not key to illustrating our claims. We will list the figures and brief explanations below.

- Figures 1 & 2: Showing examples of queue dynamics.
- Figure 3: Illustrating that CoDel has its shortcomings, providing explanations for the claim in [Throughput & Queuing Latency](#throughput--queuing-latency).

### Throughput & Queuing Latency

Claim: Titrate achieves high throughput and low latency across different traffic matrices. In comparison, CoDel and PIE achieve low latency but at the expense of throughput, while Static achieves high throughput but at the expense of latency.

Success metric: Generate `fig8.pdf` and `fig9.pdf`, and they should match `expected_fig8.pdf` and `expected_fig9.pdf` respectively.

#### Full Run

> **Caution:** A single experiment can take 5+ days and 20+GB of memory.

Run experiments (240 in total; add `--worker` to constrain the number of cores to use):

```
cd ns-3.34/
python3 ../run_ns3.py --conf ../detailed_ae/thptlat/run.conf
```

Process raw data:

```
cd detailed_ae/thptlat
python3 process_data.py
# You should expect to see data_throughput.txt and data_latency.txt.
```

Plot:

```
python3 plot_fig8.py
python3 plot_fig9.py
# You should expect to see fig8.pdf and fig9.pdf.
```

#### Quick Run

Run experiments (240 in total; take about 12 minutes on a 128-core machine):

```
cd ns-3.34/
python3 ../run_ns3.py --conf ../detailed_ae/thptlat/quickrun.conf
```

Plot:

```
cd detailed_ae/thptlat
cp expected_data_throughput.txt data_throughput.txt
cp expected_data_latency.txt data_latency.txt
python3 plot_fig8.py
python3 plot_fig9.py
# You should expect to see fig8.pdf and fig9.pdf.
```

### Burst Completion Time

Claim: Titrate achieves lower burst completion times compared to CoDel, Static and Titrate without EQlen.

Success metric: Generate `fig10.pdf`, and it should match `expected_fig10.pdf`.

Run experiments (100 in total; take about 65 minutes on a 128-core machine):

```
cd ns-3.34/
python3 ../run_ns3.py --conf ../detailed_ae/burst/run.conf
```

Process raw data:

```
cd detailed_ae/burst
python3 process_data.py
# You should expect to see data.txt.
```

Plot:

```
python3 plot_fig10.py
# You should expect to see fig10.pdf.
```

### Multiple RTTs on Multiple Ports

Claim: Titrate avoids over-allocating buffer to a queue that does not really need it (e.g., those with shorter RTT) and leaves buffer to queues that really need it (e.g., those with longer RTT), outperforming DT.

Success metric: Generate `fig11.pdf`, and it should match `expected_fig11.pdf`.

#### Full Run

> **Caution:** A single experiment can take 5+ days and 20+GB of memory.

Run experiments (8 in total):

```
cd ns-3.34/
python3 ../run_ns3.py --conf ../detailed_ae/multirtt/run.conf
```

Process raw data:

```
cd detailed_ae/multirtt
python3 process_data.py
# You should expect to see data_throughput_long.txt, data_throughput_short.txt, data_latency_long.txt, and data_latency_short.txt.
```

Plot:

```
python3 plot_fig11.py
# You should expect to see fig11.pdf.
```

#### Quick Run

Run experiments (8 in total; take about 8 minutes on a 128-core machine):

```
cd ns-3.34/
python3 ../run_ns3.py --conf ../detailed_ae/multirtt/quickrun.conf
```

Plot:

```
cd detailed_ae/multirtt
cp expected_data_throughput_long.txt data_throughput_long.txt
cp expected_data_latency_long.txt data_latency_long.txt
cp expected_data_throughput_short.txt data_throughput_short.txt
cp expected_data_latency_short.txt data_latency_short.txt
python3 plot_fig11.py
# You should expect to see fig11.pdf.
```

### Multiple Bursts on Multiple Ports

Claim: Titrate shrinks queues to the minimum buffer needed for full throughput, and thus leaves more space in the shared buffer for bursts on other ports of the same device.

Success metric: Generate `fig12.pdf`, and it should match `expected_fig12.pdf`.

#### Full Run

> **Caution:** A single experiment can take 5+ days and 20+GB of memory.

Run experiments (8 in total):

```
cd ns-3.34/
python3 ../run_ns3.py --conf ../detailed_ae/multiburst/run.conf
```

Process raw data:

```
cd detailed_ae/multiburst
python3 process_data.py
# You should expect to see data_throughput.txt, data_latency.txt, data_ndrop.txt and data_bct.txt.
```

Plot:

```
python3 plot_fig12.py
# You should expect to see fig12.pdf.
```

#### Quick Run

Run experiments (8 in total; take about 15 minutes on a 128-core machine):

```
cd ns-3.34/
python3 ../run_ns3.py --conf ../detailed_ae/multiburst/quickrun.conf
```

Plot:

```
cd detailed_ae/multiburst
cp expected_data_throughput.txt data_throughput.txt
cp expected_data_latency.txt data_latency.txt
cp expected_data_ndrop.txt data_ndrop.txt
cp expected_data_bct.txt data_bct.txt
python3 plot_fig12.py
# You should expect to see fig12.pdf.
```

### Varying Bandwidth

Claim: Titrate adapts to changing available bandwidth swiftly and efficiently.

Success metric: Generate `fig13a.pdf` and `fig13b.pdf`, and they should match `expected_fig13a.pdf` and `expected_fig13b.pdf` respectively.

#### Full Run

> **Caution:** A single experiment can take 5+ days and 20+GB of memory.

Run experiments (4 in total):

```
cd ns-3.34/
python3 ../run_ns3.py --conf ../detailed_ae/deltabw/run.conf
```

Process raw data:

```
cd detailed_ae/deltabw
python3 process_data.py
# You should expect to see data_timeseries.txt, data_throughput.txt and data_latency.txt.
```

Plot:

```
python3 plot_fig13a.py
python3 plot_fig13b.py
# You should expect to see fig13a.pdf and fig13b.pdf.
```

#### Quick Run

Run experiments (4 in total; take about 3 minutes on a 128-core machine):

```
cd ns-3.34/
python3 ../run_ns3.py --conf ../detailed_ae/deltabw/quickrun.conf
```

Plot:

```
cd detailed_ae/deltabw
cp expected_data_throughput.txt data_throughput.txt
cp expected_data_latency.txt data_latency.txt
cp expected_data_timeseries.txt data_timeseries.txt
python3 plot_fig13a.py
python3 plot_fig13b.py
# You should expect to see fig13a.pdf and fig13b.pdf.
```

### Varying Traffic

Claim: Titrate achieves high throughput and low latency with dynamic traffic.

Success metric: Generate `fig14a.pdf` and `fig14b.pdf`, and they should match `expected_fig14a.pdf` and `expected_fig14b.pdf` respectively.

#### Full Run

> **Caution:** A single experiment can take 5+ days and 20+GB of memory.

Run experiments (4 in total):

```
cd ns-3.34/
python3 ../run_ns3.py --conf ../detailed_ae/deltatraffic/run.conf
```

Process raw data:

```
cd detailed_ae/deltatraffic
python3 process_data.py
# You should expect to see data_timeseries.txt, data_throughput.txt and data_latency.txt.
```

Plot:

```
python3 plot_fig14a.py
python3 plot_fig14b.py
# You should expect to see fig14a.pdf and fig14b.pdf.
```

#### Quick Run

Run experiments (4 in total; take about 2 minutes on a 128-core machine):

```
cd ns-3.34/
python3 ../run_ns3.py --conf ../detailed_ae/deltatraffic/quickrun.conf
```

Plot:

```
cd detailed_ae/deltatraffic
cp expected_data_throughput.txt data_throughput.txt
cp expected_data_latency.txt data_latency.txt
cp expected_data_timeseries.txt data_timeseries.txt
python3 plot_fig14a.py
python3 plot_fig14b.py
# You should expect to see fig14a.pdf and fig14b.pdf.
```

### Testbed

Claim: A subset of the claim in [Throughput & Queuing Latency](#throughput--queuing-latency) but in a testbed environment with adaptive video streaming. **While we do not believe this is a key claim**, we include it to demonstrate the practicality of Titrate in real-world scenarios.

Success metric: Generate `fig15b.pdf` and `fig15c.pdf`. For `fig15b.pdf`, we expect the Threshold, EQlen, and QLen to fall over time with periodic spikes when drops occur (this figure demonstrates that Titrate does change its thresholds over time and that the code is working). For `fig15c.pdf`, we expect the static experiments to generally have a lower average bitrate than most Titrate experiments.

Note: Since this is a testbed (not simulated) experiment, the results may not match exactly due to real-world variations. Additionally, the video server is on the public internet and is subject to external network conditions.

#### Setup

Create nodes in CloudLab at the CloudLab Utah site with the following topology: https://www.cloudlab.us/p/NetSyn/titrate-ebpf.

In the `setup/` directory, there are scripts to copy to the clients and switch node respectively. The server node can be ignored (it can be utilized to run the video server, but it is not necessary for the experiment).
First, run `setup_client.sh` on the client nodes, and then run `setup_switch.sh` on the switch node.
The scripts will install the necessary dependencies.
Then, run `proxy_switch.sh` on the switch node and `proxy_client.sh` on the client nodes to proxy all non-ssh traffic from the clients through the switch node.

### Full Run

Copy the `switch/` directory to the switch node and the `client/` directory to the client nodes.
The `switch/` directory contains BPF scripts to run static and Titrate queuing disciplines on the switch node.
The `client/` directory contains scripts to run the video client on the client nodes.

To build and run the static BPF script, run:

```bash
go generate
go build -o xdp_static main.go
sudo ./xdp_static [options]
```

To build and run the Titrate BPF script, run:

```bash
go generate
go build -o xdp_titrate main.go
sudo ./xdp_titrate [options]
```

To run the video client, run:

```bash
python3 video.py [options]
```

These scripts will generate CSV files that can be used to plot the figures.

## Acknowledgements

We thank [Zili Meng](https://zilimeng.com/) and [Vamsi Addanki](https://www.vamsiaddanki.net/) for sharing their codes, which provided a foundation for this repository.
