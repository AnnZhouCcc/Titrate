#!/bin/bash
session_name="test2"
command="echo hi > test"

tmux new-session -d -s "$session_name" 'bash' # Start a detached session
tmux send-keys -t "$session_name" "$command" ENTER # Send command
#tmux attach -t "$session_name" # Attach to the session

# echo hi > test

# tmux new-session -d -s "$session_name" "$command"