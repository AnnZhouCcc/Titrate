#!/bin/bash

cd /nfs/Buffer/ns-3.34/

session_name="conf81"
command="python3 ../run_ns3.py --conf ../experiments/nsdi26sp/design_thpt/bulk1_conf81.conf"

tmux new-session -d -s "$session_name" 'bash'
tmux send-keys -t "$session_name" "$command" ENTER

session_name="conf83"
command="python3 ../run_ns3.py --conf ../experiments/nsdi26sp/design_thpt/bulk1_conf83.conf"

tmux new-session -d -s "$session_name" 'bash'
tmux send-keys -t "$session_name" "$command" ENTER

