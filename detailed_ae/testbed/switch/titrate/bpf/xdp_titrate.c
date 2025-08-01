// xdp_titrate.c - XDP program for titrate AQM algorithm

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <arpa/inet.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define NS_PER_SEC 1000000000
#define MTU 1500                                       // in bytes
#define MTU_TO_BITS(x) ((x) * MTU * 8)                 // Convert MTU to bits
#define FIXED_POINT_FACTOR 1000000                     // Factor used for fixed-point conversion
#define FIXED_TO_FLOAT(x) ((x) / (FIXED_POINT_FACTOR)) // Convert fixed point to float equivalent
#define EXTERNAL_INTERFACE_INDEX 2                     // run `ip link` to find the index of the external interface

//------------------------------------------------------------------------------

//------------------------------
// Params
//------------------------------

struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct rate_limit_params);
} rate_limit_config SEC(".maps");

// Map to store allowed interfaces
struct
{
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8); // Support up to 8 interfaces
    __type(key, __u32);     // Interface index
    __type(value, __u8);    // Just a placeholder (value is unused)
} allowed_interfaces SEC(".maps");

struct rate_limit_params
{
    __u64 rate_bps;              // Throughput rate in bits per second
    __u64 window_duration_ms;    // Window duration in milliseconds
    __u64 decrease_constant;     // Decrease constant in MTU
    __u64 min_threshold_mtu;     // Minimum threshold in MTU
    __u64 max_threshold_mtu;     // Max threshold in MTU
    __u64 initial_threshold_mtu; // Initial threshold in MTU
    __u64 windows_considered;    // Additional windows considered
    __u64 ssthresh_multiplier;   // SSThresh multiplier (stored as fixed point)
    __u64 ssthresh_enabled;      // SSThresh enabled flag
};

//------------------------------
// Buffer state
//------------------------------

struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct buffer_state);
} buffer_state SEC(".maps");

struct buffer_state
{
    __u64 current_buffer_bits;   // Current buffer usage in bits
    __u64 last_update_ns;        // Timestamp of last update
    __u64 current_threshold_mtu; // Current threshold in MTU
    __u64 ssthresh_mtu;          // Slow start threshold in MTU
};

//------------------------------
// Window State
//------------------------------
struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct window_state);
} window_state SEC(".maps");

struct window_state
{
    __u64 is_window_active;      // Flag to indicate if the window is active
    __u64 window_end_ns;         // End time of the current window
    __u64 window_zero_buffer_ns; // Time spent with zero buffer in the current window
    __u64 window_count;          // Number of windows considered (currently)
};

//------------------------------
// Queue Average
//------------------------------

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

//------------------------------
// Statistics
//------------------------------

struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct throughput_stats);
} throughput_stats SEC(".maps");

struct throughput_stats
{
    __u64 program_start_ns;     // Program start time
    __u64 total_zero_buffer_ns; // Total time with zero buffer
};

//------------------------------------------------------------------------------

SEC("xdp")
int xdp_titrate_func(struct xdp_md *ctx)
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
        // No state found, just pass the packet (TODO: May want to just abort)
        return XDP_PASS;
    }

    struct window_state *window = bpf_map_lookup_elem(&window_state, &key);
    if (!window)
    {
        // No window state found, just pass the packet (TODO: May want to just abort)
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

    // Deplete buffer with released tokens
    if (tokens_released >= state->current_buffer_bits)
    {
        // Buffer would be empty calculate time spent at zero buffer
        if (stats)
        {
            __u64 time_to_empty_ns = (state->current_buffer_bits * NS_PER_SEC) / params->rate_bps;
            __u64 time_at_zero_ns = elapsed_ns - time_to_empty_ns;
            stats->total_zero_buffer_ns += time_at_zero_ns;

            // Titrate: If the window is active, add the time spent at zero buffer
            if (window->is_window_active)
            {
                // Calculate the time spent at zero buffer within the window
                if (now <= window->window_end_ns)
                {
                    window->window_zero_buffer_ns += time_at_zero_ns;
                }
                else
                {
                    // If window end has passed, only count time up to the window end
                    __u64 time_in_window = time_at_zero_ns - (now - window->window_end_ns);
                    if (time_in_window > 0 && time_in_window <= time_at_zero_ns)
                    {
                        window->window_zero_buffer_ns += time_in_window;
                    }
                }
            }
        }
        state->current_buffer_bits = 0;
    }
    else
    {
        // Buffer is not empty decrease the buffer by the released tokens
        state->current_buffer_bits -= tokens_released;
    }

    // If the window has expired, adjust the threshold accordingly
    if (window->is_window_active && now >= window->window_end_ns)
    {

        //--------------------------------------------------------------
        // BEGIN TITRATE ALGORITHM
        // Titrate controls how the threshold changes when the buffer being
        // full causes packet drops. Otherwise, it follows the same
        // implementation of a static threshold (i.e. we always drop
        // tail and control only what the buffer size is).
        //--------------------------------------------------------------

        // Window has expired, adjust the threshold

        __u64 new_threshold_mtu;
        if (window->window_zero_buffer_ns > 0)
        {
            // Case: There was time spent at zero buffer, increase threshold

            // params->window_duration_ms / (params->window_duration_ms - window->window_zero_buffer_ns)
            __u64 window_duration_ns = (params->window_duration_ms * NS_PER_SEC / 1000) * window->window_count;
            __u64 window_zero_buffer_ns = window->window_zero_buffer_ns;

            // Calculate the new threshold
            __u64 denom = window_duration_ns - window_zero_buffer_ns;
            if (denom <= 0)
            {
                new_threshold_mtu = params->max_threshold_mtu;
            }
            else
            {
                __u64 increase_factor = (window_duration_ns * FIXED_POINT_FACTOR) / (denom);
                new_threshold_mtu = state->current_threshold_mtu * increase_factor / FIXED_POINT_FACTOR;
                if (new_threshold_mtu > params->max_threshold_mtu)
                {
                    new_threshold_mtu = params->max_threshold_mtu;
                }
            }

            // Ensure that ssthresh is not greater than the new threshold
            if (params->ssthresh_enabled)
            {
                __u64 new_ssthresh = state->current_threshold_mtu * params->ssthresh_multiplier / FIXED_POINT_FACTOR;
                if (new_ssthresh > new_threshold_mtu)
                {
                    new_ssthresh = new_threshold_mtu;
                }
                state->ssthresh_mtu = new_ssthresh;
            }

            // Increase the threshold
            state->current_threshold_mtu = new_threshold_mtu;

            // Reset the window state
            window->is_window_active = 0;
        }
        else
        {
            // Case: No time spent at zero buffer, decrease threshold
            if (params->ssthresh_enabled && state->current_threshold_mtu > state->ssthresh_mtu)
            {
                // Fast Decrease (multiplicative decrease, divide by 2)
                new_threshold_mtu = state->current_threshold_mtu / 2;
                if (new_threshold_mtu < state->ssthresh_mtu)
                {
                    new_threshold_mtu = state->ssthresh_mtu;
                }
            }
            else
            {
                // Slow Decrease (additive decrease)
                new_threshold_mtu = state->current_threshold_mtu - params->decrease_constant;
                if (new_threshold_mtu < params->min_threshold_mtu)
                {
                    new_threshold_mtu = params->min_threshold_mtu;
                }
            }
            state->current_threshold_mtu = new_threshold_mtu;

            if (window->window_count < params->windows_considered)
            {
                // Consider another round of titrate
                window->window_count++;
                window->window_end_ns = now + (params->window_duration_ms * NS_PER_SEC) / 1000;
            }
            else
            {
                // Reset the window count
                window->is_window_active = 0;
            }
        }

        //--------------------------------------------------------------
        // END TITRATE ALGORITHM
        //--------------------------------------------------------------
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

    // Drop packet if average exceeds threshold or if the absolute buffer exceeds the maximum
    if (state->current_buffer_bits + packet_size_bits > MTU_TO_BITS(params->max_threshold_mtu) || (compare_value + packet_size_bits > MTU_TO_BITS(state->current_threshold_mtu) && state->current_buffer_bits + packet_size_bits > MTU_TO_BITS(state->current_threshold_mtu)))
    {
        if (!window->is_window_active)
        {
            // If the window is not active, we need to start a new one
            window->is_window_active = 1;
            window->window_end_ns = now + (params->window_duration_ms * NS_PER_SEC) / 1000;
            window->window_zero_buffer_ns = 0;
            window->window_count = 1;
        }
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