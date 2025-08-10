// main.go
package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/link"
	"github.com/vishvananda/netlink"
)

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -cc clang xdp_static bpf/xdp_static.c -- -I/usr/include/bpf -I. -O2

type RateLimitParams struct {
	RateBps      uint64
	ThresholdMtu uint64
}

type BufferState struct {
	CurrentBufferBits uint64
	LastUpdateNs      uint64
}

type ThroughputStats struct {
	ProgramStartNs    uint64
	TotalZeroBufferNs uint64
}

type SharedStats struct {
	SimpleAvg uint64
	mu        sync.Mutex // For thread-safe access
}

const (
	NS_PER_S              = 1e9
	MTU                   = 1500 * 8 // bits
	DEFAULT_IFS           = "enX0,enX1,enX3,enX4,enX5,enX6"
	DEFAULT_RATE_KBPS     = 100000 // 100 mbps
	DEFAULT_THRESHOLD_MTU = 100

	// Userspace
	DEFAULT_READ_RATE_MS     = 50
	DEFAULT_QUEUE_SAMPLES    = 100
	DEFAULT_SAMPLE_PERIOD_US = 500 // 500us = 0.5ms
)

func calculateSimpleAverage(samples []uint64, sampleHead, sampleCount, queueSamples int) uint64 {
	var simpleSum uint64 = 0
	for i := 0; i < sampleCount; i++ {
		simpleSum += samples[(sampleHead-i-1+queueSamples)%queueSamples]
	}
	simpleAvg := simpleSum / uint64(sampleCount)
	return simpleAvg
}

func main() {
	var interfaceList string
	var rateKbps int
	var thresholdMtu int

	flag.StringVar(&interfaceList, "interfaces", DEFAULT_IFS, "Comma-separated list of network interfaces")

	// Default rate limit to 1000 Kbps (1 Mbps)
	flag.IntVar(&rateKbps, "rate", DEFAULT_RATE_KBPS, "Bandwidth in kilobits per second")

	// Default buffer size to 2 2000 Kb (2 Mbits)
	flag.IntVar(&thresholdMtu, "thresh", DEFAULT_THRESHOLD_MTU, "Static threshold in MTU")

	// Userspace parameters
	var readRateMs int
	var queueSamples int
	var samplePeriodUs int

	flag.IntVar(&readRateMs, "read_rate", DEFAULT_READ_RATE_MS, "Rate at which to read and print stats in milliseconds")
	flag.IntVar(&queueSamples, "avg_samples", DEFAULT_QUEUE_SAMPLES, "Number of queue samples to average (N)")
	flag.IntVar(&samplePeriodUs, "avg_sample_period", DEFAULT_SAMPLE_PERIOD_US, "Sampling period in microseconds")
	flag.Parse()

	if interfaceList == "" {
		fmt.Println("Please specify at least one network interface")
		flag.Usage()
		os.Exit(1)
	}

	interfaces := strings.Split(interfaceList, ",")

	// Load pre-compiled BPF program
	objs := xdp_staticObjects{}
	if err := loadXdp_staticObjects(&objs, nil); err != nil {
		log.Fatalf("Loading BPF objects failed: %v\n", err)
		os.Exit(1)
	}
	defer objs.Close()

	var links []link.Link
	for _, ifName := range interfaces {
		ifName = strings.TrimSpace(ifName)
		if ifName == "" {
			continue
		}
		ifName = strings.TrimSpace(ifName)
		if ifName == "" {
			continue
		}

		// Get interface index
		iface, err := netlink.LinkByName(ifName)
		if err != nil {
			log.Fatalf("Failed to lookup interface %s: %v\n", ifName, err)
			continue
		}

		// Add interface to allowed list
		ifIndex := uint32(iface.Attrs().Index)
		placeholder := uint8(1)
		if err := objs.AllowedInterfaces.Put(ifIndex, placeholder); err != nil {
			log.Fatalf("Failed to add interface to allowed list: %v\n", err)
			continue
		}

		// Attach XDP program to interface
		l, err := link.AttachXDP(link.XDPOptions{
			Program:   objs.XdpStaticFunc,
			Interface: iface.Attrs().Index,
			Flags:     link.XDPGenericMode,
		})
		if err != nil {
			log.Fatalf("Failed to attach XDP program to %s: %v\n", ifName, err)
			continue
		}
		links = append(links, l)

	}

	if len(links) == 0 {
		fmt.Println("Failed to attach XDP program to any interface")
		os.Exit(1)
	}

	startTimeNs := uint64(time.Now().UnixNano())

	defer func() {
		for _, l := range links {
			l.Close()
		}
	}()

	// Configure rate limiting parameters
	key := uint32(0)
	params := RateLimitParams{
		RateBps:      uint64(rateKbps) * 1000, // Convert to bits per second
		ThresholdMtu: uint64(thresholdMtu),
	}
	if err := objs.RateLimitConfig.Update(&key, &params, ebpf.UpdateAny); err != nil {
		log.Fatalf("Failed to update rate limit config: %v\n", err)
		os.Exit(1)
	}

	// Initialize buffer state
	state := BufferState{
		CurrentBufferBits: 0,
		LastUpdateNs:      0,
	}
	if err := objs.BufferState.Update(&key, &state, ebpf.UpdateAny); err != nil {
		log.Fatalf("Failed to initialize buffer state: %v\n", err)
		os.Exit(1)
	}

	// Initialize throughput statistics
	stats := ThroughputStats{
		ProgramStartNs:    0,
		TotalZeroBufferNs: 0,
	}
	if err := objs.ThroughputStats.Update(&key, &stats, ebpf.UpdateAny); err != nil {
		log.Fatalf("Failed to initialize throughput stats: %v\n", err)
		os.Exit(1)
	}

	//------------------------------------------------------------------

	avgQueueState := struct {
		AvgQueueBits uint64
	}{
		AvgQueueBits: 0,
	}

	if err := objs.AvgQueueState.Update(&key, &avgQueueState, ebpf.UpdateAny); err != nil {
		log.Fatalf("Failed to initialize average queue state: %v\n", err)
		os.Exit(1)
	}

	sharedStats := SharedStats{
		SimpleAvg: 0,
	}

	// Create a ring buffer for queue length samples (to calculate simple average)
	samples := make([]uint64, queueSamples)
	sampleHead := 0
	sampleCount := 0

	// Create a ticker for sampling
	sampleTicker := time.NewTicker(time.Duration(samplePeriodUs) * time.Microsecond)
	sampleDone := make(chan bool)

	go func() {
		for {
			select {
			case <-sampleDone:
				return
			case <-sampleTicker.C:
				var currentState BufferState
				if err := objs.BufferState.Lookup(&key, &currentState); err != nil {
					log.Printf("Error reading buffer state: %v\n", err)
					continue
				}

				// Store current buffer bits in the sample ring buffer
				samples[sampleHead] = currentState.CurrentBufferBits
				sampleHead = (sampleHead + 1) % queueSamples
				if sampleCount < queueSamples {
					sampleCount++
				}

				// Calculate outlier threshold
				simpleAvg := calculateSimpleAverage(samples, sampleHead, sampleCount, queueSamples)

				// Update shared stats
				sharedStats.mu.Lock()
				sharedStats.SimpleAvg = simpleAvg
				sharedStats.mu.Unlock()

				// Update the BPF map with the average queue length
				var avgQueueState struct {
					AvgQueueBits uint64
				}
				avgQueueState.AvgQueueBits = simpleAvg
				if err := objs.AvgQueueState.Update(&key, &avgQueueState, ebpf.UpdateAny); err != nil {
					log.Printf("Error updating average queue state: %v\n", err)
				}
			}
		}
	}()

	//------------------------------------------------------------------

	ticker := time.NewTicker(time.Duration(readRateMs) * time.Millisecond)
	done := make(chan bool)

	// Print csv header
	fmt.Printf("ElapsedTimeNs,TotalZeroBufferNs,Throughput,LastUpdateNs,CurrentBufferBits,BufferPercentage\n")

	go func() {
		for {
			select {
			case <-done:
				return
			case <-ticker.C:
				// Read current buffer state
				var currentState BufferState
				if err := objs.BufferState.Lookup(&key, &currentState); err != nil {
					log.Fatalf("Error reading buffer state: %v\n", err)
					continue
				}

				// Calculate percentage of buffer used
				bufferPercentage := float64(currentState.CurrentBufferBits) / float64(params.ThresholdMtu*MTU)

				// Fetch throughput stats
				var currentStats ThroughputStats
				var elapsedNs uint64
				if err := objs.ThroughputStats.Lookup(&key, &currentStats); err != nil {
					log.Fatalf("Error reading throughput stats: %v\n", err)
				} else {
					nowNs := uint64(time.Now().UnixNano())
					elapsedNs = nowNs - startTimeNs
				}

				throughput := 1 - (float64(currentStats.TotalZeroBufferNs) / float64(elapsedNs))

				// Print csv values
				fmt.Printf("%d,%d,%.6f,%d,%d,%.6f\n",
					elapsedNs,
					currentStats.TotalZeroBufferNs,
					throughput,
					currentState.LastUpdateNs,
					currentState.CurrentBufferBits,
					bufferPercentage,
				)
			}
		}
	}()

	// Wait for signal to exit
	c := make(chan os.Signal, 1)
	signal.Notify(c, syscall.SIGINT, syscall.SIGTERM)
	<-c

	ticker.Stop()
	done <- true
}
