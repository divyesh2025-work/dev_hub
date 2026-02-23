#!/bin/bash

FILE="test.log"

# Calculate basic statistics
echo "Average latency: $(awk '{sum+=$1} END {print sum/NR}' "$FILE") ns"

# Sort the latency values
sorted_file="sorted_latency.log"
sort -n "$FILE" > "$sorted_file"

# Function to calculate percentile
percentile() {
    PERCENT=$1
    COUNT=$(wc -l < "$sorted_file")
    INDEX=$(echo "$COUNT * $PERCENT / 100" | bc)
    awk "NR==$INDEX" "$sorted_file"
}

# Compute percentiles
echo "90th percentile: $(percentile 90) ns"
echo "95th percentile: $(percentile 95) ns"
echo "99th percentile: $(percentile 99) ns"

rm "$sorted_file"
