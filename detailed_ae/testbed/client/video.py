# video.py
# Play a DASH video in a headless browser and calculate QoE.

from selenium import webdriver
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.common.by import By
import os
import time
import argparse

# Parse command line arguments
parser = argparse.ArgumentParser(description='Play a DASH video and calculate QoE.')
parser.add_argument('--output', type=str, required=True, help='File to write output to')
parser.add_argument('--duration', type=int, required=True, help='Duration to run in seconds')
parser.add_argument('--start-time', type=int, required=True, help='Absolute start time in nanoseconds')

args = parser.parse_args()

# Path to the HTML file to be opened with the DASH player
HTML_FILE = "./dash.html"

# Time to wait between checks for new data
SLEEP_TIME = 0.1

# -----------------------------------------------------------------------

# Wait until the specified start time
current_time_ns = time.time_ns()
start_time_ns = args.start_time

if current_time_ns < start_time_ns:
    wait_time_s = (start_time_ns - current_time_ns) / 1000000000.0
    print(f"Waiting for {wait_time_s:.2f} seconds until start time...")
    time.sleep(wait_time_s)

html_file_path = os.path.abspath(HTML_FILE)

chrome_options = Options()
chrome_options.add_argument("--headless")
chrome_options.add_argument("--mute-audio")
chrome_options.add_argument("--disable-web-security")
chrome_options.add_argument("--disable-dev-shm-usage")
chrome_options.add_argument("--no-sandbox")
chrome_options.add_argument("--disable-gpu")
chrome_options.add_argument("--verbose")

driver = webdriver.Chrome(options=chrome_options)

# Use provided start time in ns
start_time = args.start_time / 1000000000.0  # Convert ns to seconds
run_until = time.time() + args.duration
rebuffer_periods = []  # List to store (start, end) pairs of rebuffer periods
last_print_time = time.time()  # Track when we last printed stats

print(f"Running for {args.duration} seconds")

try:
    # Open output file
    with open(args.output, 'w') as f_out:
        # Print header to file
        f_out.write("Name,Time,Bitrate,BitrateSwitch,RebufferTime,Buffer\n")
        
        driver.get(f"file://{html_file_path}")
        
        video = driver.find_element(By.ID, "videoPlayer")
        video.click()
        
        previous_bitrate = None
        qoe = 0
        
        while time.time() < run_until:
            is_ended = driver.execute_script(
                "return document.getElementById('videoPlayer').ended"
            )
            if is_ended:
                break
                
            # Check for rebuffer events
            rebuffer_events = driver.execute_script("return window.rebuffer_events")
            if rebuffer_events and len(rebuffer_events) > 0:
                for event in rebuffer_events:
                    event_type, timestamp = event
                    relative_time = timestamp - start_time
                    if event_type == "start":
                        rebuffer_periods.append([relative_time, None])
                    elif (
                        event_type == "end"
                        and rebuffer_periods
                        and rebuffer_periods[-1][1] is None
                    ):
                        rebuffer_periods[-1][1] = relative_time
                        f_out.write(
                            f"R{rebuffer_periods[-1][0]:.2f},{rebuffer_periods[-1][1]:.2f}\n"
                        )
                        f_out.flush()
                
                driver.execute_script("window.rebuffer_events = []")
                
            data = driver.execute_script("return window.dash_data")
                
            if data:
                latest_data = data[-1]
                url, quality, current_bitrate, buffer_level = latest_data
                elapsed_time = time.time() - start_time
                    
                # Calculate current rebuffer time only for periods that occurred since last print
                current_rebuffer_time = 0
                for period in rebuffer_periods:
                    if period[1] is not None:
                        if period[1] > last_print_time - start_time:
                            period_start = max(period[0], last_print_time - start_time)
                            current_rebuffer_time += period[1] - period_start
                    
                switch_val = 0
                if previous_bitrate is not None:
                    switch_val = abs(current_bitrate - previous_bitrate)
                    
                seg_name = url.split("/")[-1].split(".")[0]
                    
                f_out.write(
                    f"{seg_name},{elapsed_time:.2f},{current_bitrate},{switch_val:.2f},{current_rebuffer_time:.2f},{buffer_level:.2f}\n"
                )
                f_out.flush()
                print(
                    f"Time: {elapsed_time:.2f}, Bitrate: {current_bitrate}, "
                    f"Switch: {switch_val:.2f}, Rebuffer: {current_rebuffer_time:.2f}, "
                    f"Buffer Level: {buffer_level:.2f}"
                )
                    
                previous_bitrate = current_bitrate
                last_print_time = time.time()
                driver.execute_script("window.dash_data = []")
                
            time.sleep(SLEEP_TIME)
finally:
    driver.quit()