import pandas as pd
import matplotlib.pyplot as plt
import os

scheme_labels = ["Static", "Titrate"]
colorarr = ["#a3cef1", "#ffb703"]  # Static, Titrate
markerarr = ["D", "*"]

# Load data CSVs here
static_video_path = "./static_exp_video.csv"
static_fct_path = "./static_exp.csv"
titrate_video_path = "./titrate_exp_video.csv"
titrate_fct_path = "./titrate_exp.csv"

# Create directory if it doesn't exist
os.makedirs("../img", exist_ok=True)

# Load data
static_video_data = pd.read_csv(static_video_path)
static_fct_data = pd.read_csv(static_fct_path)
titrate_video_data = pd.read_csv(titrate_video_path)
titrate_fct_data = pd.read_csv(titrate_fct_path)

fig, ax = plt.subplots(figsize=(10, 4.5))

# For Static data - handle the different column names
# Create a copy of static_video_data with renamed column
static_video_data_renamed = static_video_data.copy()
static_video_data_renamed.rename(columns={"thresh": "threshold"}, inplace=True)

# Join data on threshold column
static_merged = pd.merge(static_video_data_renamed, static_fct_data, on="threshold")
static_x = static_merged["bitrate_m"] / 1e7  # Convert to Mbps by dividing by 1e7
static_y = static_merged["aggm"]  # Flow completion time in ms

# Plot Static data as scatter points
ax.scatter(
    static_x,
    static_y,
    color=colorarr[0],
    label=scheme_labels[0],
    marker=markerarr[0],
    s=250,
)  # s controls the marker size

# For Titrate data - join on window, ss, wc columns
titrate_merged = pd.merge(
    titrate_video_data, titrate_fct_data, on=["window", "ss", "wc"]
)
titrate_x = titrate_merged["bitrate_m"] / 1e7  # Convert to Mbps by dividing by 1e7
titrate_y = titrate_merged["aggm"]  # Flow completion time in ms

# Plot Titrate data as scatter points
ax.scatter(
    titrate_x,
    titrate_y,
    color=colorarr[1],
    label=scheme_labels[1],
    marker=markerarr[1],
    s=800,
)  # s controls the marker size

# Customize plot
ax.set_xlabel("Average Bitrate (mbps)", fontsize=22)
ax.set_ylabel("Flow Completion Time (ms)", fontsize=22)
ax.grid(axis="both", linestyle="--", alpha=0.7)
ax.tick_params(axis="both", labelsize=20)

ax.ticklabel_format(style="plain", axis="y")
ax.legend(loc="lower right", fontsize=24)
plt.tight_layout(pad=0.5)
plt.savefig("./fig15c.pdf", bbox_inches="tight", dpi=500, pad_inches=0.1)
plt.show()
