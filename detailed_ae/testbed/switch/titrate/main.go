// Package main implements an XDP-based traffic shaper using the Titrate AQM (Active Queue Management) algorithm.
// Command-line options allow fine-tuning of all algorithm parameters.
// Outputs real-time performance statistics in CSV format.
// Run with -h for help.

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

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -cc clang xdp_titrate bpf/xdp_titrate.c -- -I/usr/include/bpf -I. -O2

type RateLimitParams struct {
	RateBps             uint64
	WindowDurationMs    uint64
	DecreaseConstant    uint64
	MinThresholdMtu     uint64
	MaxThresholdMtu     uint64
	InitialThresholdMtu uint64
	WindowsConsidered   uint64
	SsthreshMultiplier  uint64 // We'll store this as fixed point
	SsthreshEnabled     uint64
}

type BufferState struct {
	CurrentBufferBits   uint64
	LastUpdateNs        uint64
	CurrentThresholdMtu uint64
	SsthreshMtu         uint64
}

type ThroughputStats struct {
	ProgramStartNs    uint64
	TotalZeroBufferNs uint64
}

type SharedStats struct {
	SimpleAvg   uint64
	FilteredAvg uint64
	mu          sync.Mutex // For thread-safe access
}

const (
	// Constants
	MTU                    = 1500 * 8 // bits
	NS_PER_S               = 1e9
	TO_FIXED_PT_MULTIPLIER = 1e6

	// Kernelspace
	DEFAULT_IFS                   = "enX0,enX1,enX3,enX4,enX5,enX6" // To all client interfaces
	DEFAULT_RATE_KBPS             = 100000                          // 100 mbps
	DEFAULT_WINDOW_DURATION_MS    = 200                             // 100ms to 600ms range
	DEFAULT_DECREASE_CONSTANT     = 1                               // Recommended low integer
	DEFAULT_MIN_THRESHOLD_MTU     = 2                               // Recommended low integer, must be nonzero
	DEFAULT_MAX_THRESHOLD_MTU     = 100                             // Recommended ~2BDP
	DEFAULT_INITIAL_THRESHOLD_MTU = 50                              // Recommended half of max (i.e. ~1BDP)
	DEFAULT_WINDOWS_CONSIDERED    = 1                               // Seemingly not very useful, but set to 1 to disable
	DEFAULT_SSHRESH_MULTIPLIER    = 1.5                             // must be >= 1.0
	DEFAULT_SSTHRESH_ENABLED      = 0                               // must be 0 or 1

	// Userspace
	DEFAULT_READ_RATE_MS     = 50
	DEFAULT_QUEUE_SAMPLES    = 100
	DEFAULT_SAMPLE_PERIOD_US = 500 // 500us = 0.5ms
)

//------------------------------------------------------------------------------
// Helper Functions
//------------------------------------------------------------------------------

// Calculate outlier threshold using specified method
func calculateOutlierThreshold(samples []uint64, sampleHead, sampleCount, queueSamples int) (uint64, uint64) {
	// Calculate simple average first
	var simpleSum uint64 = 0
	for i := 0; i < sampleCount; i++ {
		simpleSum += samples[(sampleHead-i-1+queueSamples)%queueSamples]
	}
	simpleAvg := simpleSum / uint64(sampleCount)

	// Calculate outlier threshold
	var outlierThresh uint64
	outlierThresh = 2 * simpleAvg

	return outlierThresh, simpleAvg
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------

func main() {
	// Parameters for the XDP program
	var interfaceList string
	var rateKbps int
	var windowDurationMs int
	var decreaseConstant int
	var minThresholdMtu int
	var maxThresholdMtu int // Should be calculated to be ~2BDP
	var initialThresholdMtu int
	var windowsConsidered int
	var ssthreshMultiplier float64
	var ssthreshEnabled int

	flag.StringVar(&interfaceList, "interfaces", DEFAULT_IFS, "Comma-separated list of network interfaces")
	flag.IntVar(&rateKbps, "rate", DEFAULT_RATE_KBPS, "Bandwidth in kilobits per second")
	flag.IntVar(&windowDurationMs, "window", DEFAULT_WINDOW_DURATION_MS, "Window duration in milliseconds")
	flag.IntVar(&decreaseConstant, "decrease", DEFAULT_DECREASE_CONSTANT, "Decrease constant in MTU")
	flag.IntVar(&minThresholdMtu, "thresh_min", DEFAULT_MIN_THRESHOLD_MTU, "Minimum threshold in MTU")
	flag.IntVar(&initialThresholdMtu, "thresh_init", DEFAULT_INITIAL_THRESHOLD_MTU, "Initial threshold in MTU")
	flag.IntVar(&maxThresholdMtu, "thresh_max", DEFAULT_MAX_THRESHOLD_MTU, "Maximum threshold in MTU")
	flag.IntVar(&windowsConsidered, "windows_considered", DEFAULT_WINDOWS_CONSIDERED, "Number of windows considered [optimization, set to 1 to disable]")
	flag.Float64Var(&ssthreshMultiplier, "ss_mult", DEFAULT_SSHRESH_MULTIPLIER, "SSThresh multiplier [optimization]")
	flag.IntVar(&ssthreshEnabled, "ss_on", DEFAULT_SSTHRESH_ENABLED, "SSThresh enabled (0 or 1) [optimization]")

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
	objs := xdp_titrateObjects{}
	if err := loadXdp_titrateObjects(&objs, nil); err != nil {
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
			Program:   objs.XdpTitrateFunc,
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
		RateBps:             uint64(rateKbps) * 1000, // Convert to bits per second
		WindowDurationMs:    uint64(windowDurationMs),
		DecreaseConstant:    uint64(decreaseConstant),
		MinThresholdMtu:     uint64(minThresholdMtu),
		MaxThresholdMtu:     uint64(maxThresholdMtu),
		InitialThresholdMtu: uint64(initialThresholdMtu),
		WindowsConsidered:   uint64(windowsConsidered),
		SsthreshMultiplier:  uint64(ssthreshMultiplier * TO_FIXED_PT_MULTIPLIER),
		SsthreshEnabled:     uint64(ssthreshEnabled),
	}
	if err := objs.RateLimitConfig.Update(&key, &params, ebpf.UpdateAny); err != nil {
		log.Fatalf("Failed to update rate limit config: %v\n", err)
		os.Exit(1)
	}

	// Initialize buffer state
	state := BufferState{
		CurrentBufferBits:   0,
		LastUpdateNs:        0,
		CurrentThresholdMtu: uint64(initialThresholdMtu),
		SsthreshMtu:         uint64(initialThresholdMtu),
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
	// Queue Length Average Calculation
	//------------------------------------------------------------------

	// Initialize average queue state
	avgQueueState := struct {
		AvgQueueBits uint64
	}{
		AvgQueueBits: 0, // Start with zero average
	}

	if err := objs.AvgQueueState.Update(&key, &avgQueueState, ebpf.UpdateAny); err != nil {
		log.Fatalf("Failed to initialize average queue state: %v\n", err)
		os.Exit(1)
	}

	sharedStats := SharedStats{
		SimpleAvg:   0,
		FilteredAvg: 0,
	}

	// Create a ring buffer for queue length samples (to calculate filtered average)
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
				outlierThresh, simpleAvg := calculateOutlierThreshold(samples, sampleHead, sampleCount, queueSamples)

				// Filter outliers
				var filteredSum uint64 = 0
				var filteredCount uint64 = 0
				for i := 0; i < sampleCount; i++ {
					if samples[(sampleHead-i-1+queueSamples)%queueSamples] < outlierThresh {
						filteredSum += samples[(sampleHead-i-1+queueSamples)%queueSamples]
						filteredCount++
					}
				}
				if filteredCount == 0 {
					filteredCount = 1
				}
				filteredAvg := filteredSum / filteredCount

				// Update shared stats
				sharedStats.mu.Lock()
				sharedStats.SimpleAvg = simpleAvg
				sharedStats.FilteredAvg = filteredAvg
				sharedStats.mu.Unlock()

				// Update the BPF map with the average queue length
				var avgQueueState struct {
					AvgQueueBits uint64
				}
				avgQueueState.AvgQueueBits = filteredAvg
				if err := objs.AvgQueueState.Update(&key, &avgQueueState, ebpf.UpdateAny); err != nil {
					log.Printf("Error updating average queue state: %v\n", err)
				}
			}
		}
	}()

	//------------------------------------------------------------------
	// Read and Print Stats
	//------------------------------------------------------------------

	statsTicker := time.NewTicker(time.Duration(readRateMs) * time.Millisecond)
	statsDone := make(chan bool)

	// Print csv header
	fmt.Printf("ElapsedTimeNs,TotalZeroBufferNs,Throughput,LastUpdateNs,CurrentThresholdMtu,SsthreshMtu,CurrentBufferBits,SimpleAvgBits,FilteredAvgBits,BufferPercentage\n")

	// Goroutine to read and print stats
	go func() {
		for {
			select {
			case <-statsDone:
				return
			case <-statsTicker.C:
				// Read current buffer state
				var currentState BufferState
				if err := objs.BufferState.Lookup(&key, &currentState); err != nil {
					log.Fatalf("Error reading buffer state: %v\n", err)
					continue
				}

				// Calculate percentage of buffer used
				bufferPercentage := float64(currentState.CurrentBufferBits) / float64(currentState.CurrentThresholdMtu*MTU)

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

				// Get the current outlier threshold and simple average
				sharedStats.mu.Lock()
				filteredAvg := sharedStats.FilteredAvg
				simpleAvg := sharedStats.SimpleAvg
				sharedStats.mu.Unlock()

				// Print csv values
				fmt.Printf("%d,%d,%.6f,%d,%d,%d,%d,%d,%d,%.6f\n",
					elapsedNs,
					currentStats.TotalZeroBufferNs,
					throughput,
					currentState.LastUpdateNs,
					currentState.CurrentThresholdMtu,
					currentState.SsthreshMtu,
					currentState.CurrentBufferBits,
					simpleAvg,
					filteredAvg,
					bufferPercentage,
				)
			}
		}
	}()

	// Wait for signal to exit
	c := make(chan os.Signal, 1)
	signal.Notify(c, syscall.SIGINT, syscall.SIGTERM)
	<-c

	statsTicker.Stop()
	statsDone <- true
	sampleTicker.Stop()
	sampleDone <- true
}
