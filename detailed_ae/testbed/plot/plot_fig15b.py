import pandas as pd
import matplotlib.pyplot as plt
import os
import sys


def plot_mtu_over_time(csv_path):
    # Read CSV data
    column_names = [
        "ElapsedTimeNs",
        "TotalZeroBufferNs",
        "Throughput",
        "LastUpdateNs",
        "CurrentThresholdMtu",
        "SsthreshMtu",
        "CurrentBufferBits",
        "SimpleAvgBits",
        "FilteredAvgBits",
        "BufferPercentage",
    ]

    try:
        # Try reading with headers
        df = pd.read_csv(csv_path)
    except:
        try:
            # Try reading without headers
            df = pd.read_csv(csv_path, names=column_names)
        except Exception as e:
            print(f"Error reading CSV file: {e}")
            return

    # Make sure we have the expected columns
    for col in [
        "ElapsedTimeNs",
        "CurrentThresholdMtu",
        "FilteredAvgBits",
        "CurrentBufferBits",
    ]:
        if col not in df.columns:
            print(f"Error: Required column '{col}' not found in CSV")
            return

    # Convert all relevant columns to numeric, errors='coerce' will convert non-numeric values to NaN
    numeric_columns = [
        "ElapsedTimeNs",
        "CurrentThresholdMtu",
        "FilteredAvgBits",
        "CurrentBufferBits",
    ]
    for col in numeric_columns:
        df[col] = pd.to_numeric(df[col], errors="coerce")

    # Drop rows with NaN values
    df = df.dropna(subset=numeric_columns)

    # Convert elapsed time from nanoseconds to seconds
    df["TimeS"] = df["ElapsedTimeNs"] / 1e9

    # Filter to only include data from the first 30 seconds
    df = df[df["TimeS"] <= 30]

    # Convert bits to MTU units (1 MTU = 1500*8 bits)
    mtu_conversion = 1500 * 8
    df["FilteredAvgMtu"] = df["FilteredAvgBits"] / mtu_conversion
    df["CurrentBufferMtu"] = df["CurrentBufferBits"] / mtu_conversion

    fig, ax = plt.subplots(figsize=(12, 6))

    ax.plot(
        df["TimeS"],
        df["CurrentThresholdMtu"],
        color="#fb8500",
        linewidth=1.5,
        label="Threshold",
    )

    ax.plot(
        df["TimeS"],
        df["FilteredAvgMtu"],
        color="#ffb703",
        linewidth=1.5,
        label="Filtered Average Queue Length",
    )

    ax.plot(
        df["TimeS"],
        df["CurrentBufferMtu"],
        color="#2a9d8f",
        linewidth=1,
        label="Queue Length",
    )

    ax.set_xlabel("Time (s)", fontsize=24)
    ax.set_ylabel("MTU", fontsize=24)
    ax.grid(axis="both", linestyle="--", alpha=0.7)
    ax.tick_params(axis="both", labelsize=20)

    ax.set_xlim(0, 30)
    ax.legend(loc="lower right", fontsize=20)

    plt.tight_layout()
    plt.savefig("./fig15b.pdf", bbox_inches="tight", dpi=500)
    plt.show()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script.py <path_to_csv>")
        sys.exit(1)

    csv_path = sys.argv[1]
    plot_mtu_over_time(csv_path)
