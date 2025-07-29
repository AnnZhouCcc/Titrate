#!/bin/bash

DIR="$1"

# Check if directory is given
if [ -z "$DIR" ]; then
  echo "Usage: $0 /path/to/directory"
  exit 1
fi

# Generate timestamped filename in the specified directory
TIMESTAMP=$(date "+%Y-%m-%d_%H-%M-%S")
OUTPUT_FILE="$DIR/$TIMESTAMP.tr"

# Loop through all regular files and write output to the file
find "$DIR" -type f | while read -r file; do
  {
    echo "==> $file"
    tail -n 1 "$file"
    echo
  } >> "$OUTPUT_FILE"
done

echo "Output written to $OUTPUT_FILE"