#!/bin/bash

cd /nfs/Buffer/ns-3.34/

session_name="conf65"
command="python3 ../run_ns3.py --conf ../experiments/nsdi26sp/design_thpt/bulk1_conf65.conf"

tmux new-session -d -s "$session_name" 'bash'
tmux send-keys -t "$session_name" "$command" ENTER

session_name="conf67"
command="python3 ../run_ns3.py --conf ../experiments/nsdi26sp/design_thpt/bulk1_conf67.conf"

tmux new-session -d -s "$session_name" 'bash'
tmux send-keys -t "$session_name" "$command" ENTER

