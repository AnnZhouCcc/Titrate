// xdp_static.c - XDP program for static buffer threshold

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <arpa/inet.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define NS_PER_SEC 1000000000
#define MTU 1500
#define MTU_TO_BITS(x) ((x) * MTU * 8) // Convert MTU to bits
#define EXTERNAL_INTERFACE_INDEX 2     // run `ip link` to find the index of the external interface

//------------------------------------------------------------------------------

// Map to store rate limiting parameters
struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct rate_limit_params);
} rate_limit_config SEC(".maps");

// Map to store current buffer state
struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct buffer_state);
} buffer_state SEC(".maps");

// Map to store allowed interfaces
struct
{
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8); // Support up to 8 interfaces
    __type(key, __u32);     // Interface index
    __type(value, __u8);    // Just a placeholder (value is unused)
} allowed_interfaces SEC(".maps");

// Define parameters structure
struct rate_limit_params
{
    __u64 rate_bps;      // Rate in bits per second
    __u64 threshold_mtu; // Threshold in MTU
};

// Define buffer state structure
struct buffer_state
{
    __u64 current_buffer_bits; // Current buffer usage in bits
    __u64 last_update_ns;      // Timestamp of last update
};

struct avg_queue_state
{
    __u64 avg_queue_bits; // Average queue size in bits over a window (calculated in userspace)
};

struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct avg_queue_state);
} avg_queue_state SEC(".maps");

// Map to store throughput statistics
struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct throughput_stats);
} throughput_stats SEC(".maps");

// Define statistics structure
struct throughput_stats
{
    __u64 program_start_ns;     // Program start time
    __u64 total_zero_buffer_ns; // Total time with zero buffer
};

//------------------------------------------------------------------------------

SEC("xdp")
int xdp_static_func(struct xdp_md *ctx)
{
    __u32 ingress_ifindex = ctx->ingress_ifindex;
    __u8 *allowed = bpf_map_lookup_elem(&allowed_interfaces, &ingress_ifindex);
    if (!allowed)
    {
        // Interface not allowed, drop packet
        return XDP_DROP;
    }

    __u32 key = 0;
    struct rate_limit_params *params = bpf_map_lookup_elem(&rate_limit_config, &key);
    if (!params)
    {
        // No configuration, just pass the packet
        return XDP_PASS;
    }

    struct buffer_state *state = bpf_map_lookup_elem(&buffer_state, &key);
    if (!state)
    {
        // No state found, just pass the packet
        return XDP_PASS;
    }

    // Get packet size
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;

    if (data + sizeof(struct ethhdr) > data_end)
    {
        // Malformed packet
        return XDP_PASS;
    }

    // Check for SSH traffic on external interface (enX0)
    if (ingress_ifindex == EXTERNAL_INTERFACE_INDEX)
    {
        struct ethhdr *eth = data;
        if (eth->h_proto == bpf_htons(ETH_P_IP))
        {
            if (data + sizeof(struct ethhdr) + sizeof(struct iphdr) > data_end)
            {
                return XDP_PASS; // Packet too small to check
            }

            struct iphdr *iph = (struct iphdr *)(data + sizeof(struct ethhdr));
            if (iph->protocol == IPPROTO_TCP)
            { // Use #define IPPROTO_TCP 6
                if (data + sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct tcphdr) > data_end)
                {
                    return XDP_PASS; // Packet too small to check
                }

                struct tcphdr *tcph = (struct tcphdr *)(data + sizeof(struct ethhdr) + sizeof(struct iphdr));
                if (bpf_ntohs(tcph->dest) == 22 || bpf_ntohs(tcph->source) == 22)
                {
                    // SSH traffic - pass it through without processing
                    return XDP_PASS;
                }
            }
        }
    }

    // Calculate packet size in bits
    __u64 packet_size_bits = (data_end - data) * 8;

    // Get current time
    __u64 now = bpf_ktime_get_ns();

    struct throughput_stats *stats = bpf_map_lookup_elem(&throughput_stats, &key);
    if (stats && stats->program_start_ns == 0)
    {
        stats->program_start_ns = now;
    }

    __u64 elapsed_ns;
    // Initialize last_update_ns if it's zero
    if (state->last_update_ns == 0)
    {
        state->last_update_ns = now;
        elapsed_ns = 0; // We start with no elapsed time
    }
    else
    {
        elapsed_ns = now - state->last_update_ns;
    }
    __u64 tokens_released = (elapsed_ns * params->rate_bps) / NS_PER_SEC;

    // Update buffer state
    if (tokens_released >= state->current_buffer_bits)
    {
        // Buffer would be empty
        // Calculate time spent at zero buffer
        if (stats)
        {
            __u64 time_to_empty_ns = (state->current_buffer_bits * NS_PER_SEC) / params->rate_bps;
            __u64 time_at_zero_ns = elapsed_ns - time_to_empty_ns;
            stats->total_zero_buffer_ns += time_at_zero_ns;
        }
        state->current_buffer_bits = 0;
    }
    else
    {
        // Buffer is not empty
        // Decrease the buffer by the released tokens
        state->current_buffer_bits -= tokens_released;
    }

    // Check if we have enough buffer space
    struct avg_queue_state *avg_queue = bpf_map_lookup_elem(&avg_queue_state, &key);
    __u64 compare_value;
    if (!avg_queue)
    {
        // No average queue, default to current buffer
        compare_value = state->current_buffer_bits;
    }
    else
    {
        // Use the average queue size
        compare_value = avg_queue->avg_queue_bits;
    }

    if (state->current_buffer_bits + packet_size_bits > MTU_TO_BITS(params->threshold_mtu) && compare_value + packet_size_bits > MTU_TO_BITS(params->threshold_mtu))
    {
        // Buffer would overflow, drop packet
        state->last_update_ns = now;
        return XDP_DROP;
    }
    else
    {
        // Accept the packet and update buffer state
        state->current_buffer_bits += packet_size_bits;
        state->last_update_ns = now;

        return XDP_PASS;
    }
}

//------------------------------------------------------------------------------

char _license[] SEC("license") = "GPL";