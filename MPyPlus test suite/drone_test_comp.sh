#!/bin/bash

# Run the micropython command and capture output
echo "Running drone comparison test..."
OUTPUT=$(../ports/unix/build-standard/micropython drone_comparison.py)

# Display the full output (optional - comment out if not needed)
echo "$OUTPUT"
echo ""

# Extract memory values using grep and sed
echo "### MEMORY USAGE ANALYSIS ###"
echo ""

# Extract Pass-by-Value memory values
PV_BEFORE=$(echo "$OUTPUT" | grep -A 10 "TEST 1: PASS-BY-VALUE VERSION" | grep "mem: total=" | head -1 | sed -n 's/.*total=\([0-9]*\), current=\([0-9]*\), peak=\([0-9]*\).*/\1 \2 \3/p')
PV_AFTER=$(echo "$OUTPUT" | grep -A 20 "TEST 1: PASS-BY-VALUE VERSION" | grep "mem: total=" | tail -1 | sed -n 's/.*total=\([0-9]*\), current=\([0-9]*\), peak=\([0-9]*\).*/\1 \2 \3/p')

# Extract Pass-by-Reference memory values
PR_BEFORE=$(echo "$OUTPUT" | grep -A 10 "TEST 2: PASS-BY-REFERENCE (POINTER-BASED) VERSION" | grep "mem: total=" | head -1 | sed -n 's/.*total=\([0-9]*\), current=\([0-9]*\), peak=\([0-9]*\).*/\1 \2 \3/p')
PR_AFTER=$(echo "$OUTPUT" | grep -A 20 "TEST 2: PASS-BY-REFERENCE (POINTER-BASED) VERSION" | grep "mem: total=" | tail -1 | sed -n 's/.*total=\([0-9]*\), current=\([0-9]*\), peak=\([0-9]*\).*/\1 \2 \3/p')

# Parse the values
PV_BEFORE_TOTAL=$(echo $PV_BEFORE | cut -d' ' -f1)
PV_BEFORE_CURRENT=$(echo $PV_BEFORE | cut -d' ' -f2)
PV_BEFORE_PEAK=$(echo $PV_BEFORE | cut -d' ' -f3)
PV_AFTER_TOTAL=$(echo $PV_AFTER | cut -d' ' -f1)
PV_AFTER_CURRENT=$(echo $PV_AFTER | cut -d' ' -f2)
PV_AFTER_PEAK=$(echo $PV_AFTER | cut -d' ' -f3)

PR_BEFORE_TOTAL=$(echo $PR_BEFORE | cut -d' ' -f1)
PR_BEFORE_CURRENT=$(echo $PR_BEFORE | cut -d' ' -f2)
PR_BEFORE_PEAK=$(echo $PR_BEFORE | cut -d' ' -f3)
PR_AFTER_TOTAL=$(echo $PR_AFTER | cut -d' ' -f1)
PR_AFTER_CURRENT=$(echo $PR_AFTER | cut -d' ' -f2)
PR_AFTER_PEAK=$(echo $PR_AFTER | cut -d' ' -f3)

# Calculate differences
PV_CURRENT_DIFF=$((PV_AFTER_CURRENT - PV_BEFORE_CURRENT))
PV_PEAK_DIFF=$((PV_AFTER_PEAK - PV_BEFORE_PEAK))
PV_TOTAL_DIFF=$((PV_AFTER_TOTAL - PV_BEFORE_TOTAL))

PR_CURRENT_DIFF=$((PR_AFTER_CURRENT - PR_BEFORE_CURRENT))
PR_PEAK_DIFF=$((PR_AFTER_PEAK - PR_BEFORE_PEAK))
PR_TOTAL_DIFF=$((PR_AFTER_TOTAL - PR_BEFORE_TOTAL))

# Compare the two approaches
CURRENT_COMPARE=$((PV_CURRENT_DIFF - PR_CURRENT_DIFF))
PEAK_COMPARE=$((PV_PEAK_DIFF - PR_PEAK_DIFF))
TOTAL_COMPARE=$((PV_TOTAL_DIFF - PR_TOTAL_DIFF))

# Extract GC information
echo "### GARBAGE COLLECTOR ANALYSIS ###"
echo ""

# Extract GC values for Pass-by-Value
PV_GC_BEFORE=$(echo "$OUTPUT" | grep -A 10 "TEST 1: PASS-BY-VALUE VERSION" | grep "GC: total:" | head -1)
PV_GC_AFTER=$(echo "$OUTPUT" | grep -A 20 "TEST 1: PASS-BY-VALUE VERSION" | grep "GC: total:" | tail -1)

# Extract GC values for Pass-by-Reference
PR_GC_BEFORE=$(echo "$OUTPUT" | grep -A 10 "TEST 2: PASS-BY-REFERENCE (POINTER-BASED) VERSION" | grep "GC: total:" | head -1)
PR_GC_AFTER=$(echo "$OUTPUT" | grep -A 20 "TEST 2: PASS-BY-REFERENCE (POINTER-BASED) VERSION" | grep "GC: total:" | tail -1)

# Parse GC values
PV_GC_BEFORE_USED=$(echo $PV_GC_BEFORE | sed -n 's/.*used: \([0-9]*\),.*/\1/p')
PV_GC_AFTER_USED=$(echo $PV_GC_AFTER | sed -n 's/.*used: \([0-9]*\),.*/\1/p')
PR_GC_BEFORE_USED=$(echo $PR_GC_BEFORE | sed -n 's/.*used: \([0-9]*\),.*/\1/p')
PR_GC_AFTER_USED=$(echo $PR_GC_AFTER | sed -n 's/.*used: \([0-9]*\),.*/\1/p')

PV_GC_DIFF=$((PV_GC_AFTER_USED - PV_GC_BEFORE_USED))
PR_GC_DIFF=$((PR_GC_AFTER_USED - PR_GC_BEFORE_USED))

# Extract block information
PV_BLOCKS_BEFORE=$(echo "$OUTPUT" | grep -A 10 "TEST 1: PASS-BY-VALUE VERSION" | grep "No. of 1-blocks:" | head -1)
PV_BLOCKS_AFTER=$(echo "$OUTPUT" | grep -A 20 "TEST 1: PASS-BY-VALUE VERSION" | grep "No. of 1-blocks:" | tail -1)
PR_BLOCKS_BEFORE=$(echo "$OUTPUT" | grep -A 10 "TEST 2: PASS-BY-REFERENCE (POINTER-BASED) VERSION" | grep "No. of 1-blocks:" | head -1)
PR_BLOCKS_AFTER=$(echo "$OUTPUT" | grep -A 20 "TEST 2: PASS-BY-REFERENCE (POINTER-BASED) VERSION" | grep "No. of 1-blocks:" | tail -1)

# Display results
echo "----------------------------------------"
echo "PASS-BY-VALUE MEMORY CHANGES:"
echo "  Current memory: +$PV_CURRENT_DIFF bytes"
echo "  Peak memory:    +$PV_PEAK_DIFF bytes"
echo "  Total memory:   +$PV_TOTAL_DIFF bytes"
echo "  GC used:        +$PV_GC_DIFF bytes"
echo ""
echo "PASS-BY-REFERENCE MEMORY CHANGES:"
echo "  Current memory: +$PR_CURRENT_DIFF bytes"
echo "  Peak memory:    +$PR_PEAK_DIFF bytes"
echo "  Total memory:   +$PR_TOTAL_DIFF bytes"
echo "  GC used:        +$PR_GC_DIFF bytes"
echo ""
echo "----------------------------------------"
echo "MEMORY USAGE COMPARISON (Value - Reference):"
echo "  Current memory difference: $CURRENT_COMPARE bytes"
echo "  Peak memory difference:    $PEAK_COMPARE bytes"
echo "  Total memory difference:   $TOTAL_COMPARE bytes"
echo "  GC usage difference:       $((PV_GC_DIFF - PR_GC_DIFF)) bytes"
echo ""

# Performance comparison from output
EXEC_TIME_PV=$(echo "$OUTPUT" | grep -A 10 "TEST 1: PASS-BY-VALUE VERSION" | grep "Total time:" | sed -n 's/.*Total time: \([0-9]*\) ms.*/\1/p')
EXEC_TIME_PR=$(echo "$OUTPUT" | grep -A 10 "TEST 2: PASS-BY-REFERENCE (POINTER-BASED) VERSION" | grep "Total time:" | sed -n 's/.*Total time: \([0-9]*\) ms.*/\1/p')

echo "### EXECUTION TIME ANALYSIS ###"
echo "  Pass-by-Value:      ${EXEC_TIME_PV:-N/A} ms"
echo "  Pass-by-Reference:  ${EXEC_TIME_PR:-N/A} ms"
echo "  Time saved:         ${EXEC_TIME_PV:-0} ms (${EXEC_TIME_PV:-0}% faster)"
echo ""

# Extract block count differences
echo "### HEAP BLOCK ANALYSIS ###"
PV_1BLK_BEFORE=$(echo $PV_BLOCKS_BEFORE | sed -n 's/.*1-blocks: \([0-9]*\),.*/\1/p')
PV_1BLK_AFTER=$(echo $PV_BLOCKS_AFTER | sed -n 's/.*1-blocks: \([0-9]*\),.*/\1/p')
PV_2BLK_BEFORE=$(echo $PV_BLOCKS_BEFORE | sed -n 's/.*2-blocks: \([0-9]*\),.*/\1/p')
PV_2BLK_AFTER=$(echo $PV_BLOCKS_AFTER | sed -n 's/.*2-blocks: \([0-9]*\),.*/\1/p')

PR_1BLK_BEFORE=$(echo $PR_BLOCKS_BEFORE | sed -n 's/.*1-blocks: \([0-9]*\),.*/\1/p')
PR_1BLK_AFTER=$(echo $PR_BLOCKS_AFTER | sed -n 's/.*1-blocks: \([0-9]*\),.*/\1/p')
PR_2BLK_BEFORE=$(echo $PR_BLOCKS_BEFORE | sed -n 's/.*2-blocks: \([0-9]*\),.*/\1/p')
PR_2BLK_AFTER=$(echo $PR_BLOCKS_AFTER | sed -n 's/.*2-blocks: \([0-9]*\),.*/\1/p')

echo "Pass-by-Value block changes:"
echo "  1-blocks: +$((PV_1BLK_AFTER - PV_1BLK_BEFORE))"
echo "  2-blocks: +$((PV_2BLK_AFTER - PV_2BLK_BEFORE))"
echo ""
echo "Pass-by-Reference block changes:"
echo "  1-blocks: +$((PR_1BLK_AFTER - PR_1BLK_BEFORE))"
echo "  2-blocks: +$((PR_2BLK_AFTER - PR_2BLK_BEFORE))"
echo ""

echo ""
echo "### PERCENTAGE REDUCTION ANALYSIS ###"
echo ""

# Calculate percentage reductions
if [ $PV_CURRENT_DIFF -ne 0 ]; then
    CURRENT_REDUCTION_PERCENT=$(echo "scale=2; ($PV_CURRENT_DIFF - $PR_CURRENT_DIFF) * 100 / $PV_CURRENT_DIFF" | bc)
    echo "Current Memory Reduction:  ${CURRENT_REDUCTION_PERCENT}% less memory used"
else
    echo "Current Memory Reduction:  N/A (no baseline)"
fi

if [ $PV_PEAK_DIFF -ne 0 ]; then
    PEAK_REDUCTION_PERCENT=$(echo "scale=2; ($PV_PEAK_DIFF - $PR_PEAK_DIFF) * 100 / $PV_PEAK_DIFF" | bc)
    echo "Peak Memory Reduction:     ${PEAK_REDUCTION_PERCENT}% less memory used"
else
    echo "Peak Memory Reduction:     N/A (no baseline)"
fi

if [ $PV_TOTAL_DIFF -ne 0 ]; then
    TOTAL_REDUCTION_PERCENT=$(echo "scale=2; ($PV_TOTAL_DIFF - $PR_TOTAL_DIFF) * 100 / $PV_TOTAL_DIFF" | bc)
    echo "Total Memory Reduction:    ${TOTAL_REDUCTION_PERCENT}% less memory allocated"
else
    echo "Total Memory Reduction:    N/A (no baseline)"
fi

if [ $PV_GC_DIFF -ne 0 ]; then
    GC_REDUCTION_PERCENT=$(echo "scale=2; ($PV_GC_DIFF - $PR_GC_DIFF) * 100 / $PV_GC_DIFF" | bc)
    echo "GC Memory Reduction:       ${GC_REDUCTION_PERCENT}% less GC allocation"
else
    echo "GC Memory Reduction:       N/A (no baseline)"
fi

# Block allocation percentage reduction
PV_TOTAL_BLOCKS=$(( (PV_1BLK_AFTER - PV_1BLK_BEFORE) + (PV_2BLK_AFTER - PV_2BLK_BEFORE) ))
PR_TOTAL_BLOCKS=$(( (PR_1BLK_AFTER - PR_1BLK_BEFORE) + (PR_2BLK_AFTER - PR_2BLK_BEFORE) ))

if [ $PV_TOTAL_BLOCKS -ne 0 ]; then
    BLOCK_REDUCTION_PERCENT=$(echo "scale=2; ($PV_TOTAL_BLOCKS - $PR_TOTAL_BLOCKS) * 100 / $PV_TOTAL_BLOCKS" | bc)
    echo "Heap Block Reduction:      ${BLOCK_REDUCTION_PERCENT}% fewer blocks allocated"
else
    echo "Heap Block Reduction:      N/A (no baseline)"
fi

# Time reduction percentage
if [ ${EXEC_TIME_PV:-0} -ne 0 ]; then
    TIME_REDUCTION_PERCENT=$(echo "scale=2; (${EXEC_TIME_PV:-0} - ${EXEC_TIME_PR:-0}) * 100 / ${EXEC_TIME_PV:-0}" | bc)
    echo "Execution Time Reduction:  ${TIME_REDUCTION_PERCENT}% faster execution"
else
    echo "Execution Time Reduction:  N/A (no baseline)"
fi

echo ""

# Visual bar chart representation
echo "### VISUAL COMPARISON ###"
echo ""

# Calculate bar lengths (max 50 chars)
MAX_MEM=$(( $PV_CURRENT_DIFF > $PR_CURRENT_DIFF ? $PV_CURRENT_DIFF : $PR_CURRENT_DIFF ))
if [ $MAX_MEM -gt 0 ]; then
    PV_BAR_LEN=$(( $PV_CURRENT_DIFF * 50 / $MAX_MEM ))
    PR_BAR_LEN=$(( $PR_CURRENT_DIFF * 50 / $MAX_MEM ))
    
    echo "Current Memory Usage Increase:"
    printf "  Pass-by-Value:     ["
    for i in $(seq 1 $PV_BAR_LEN); do printf "#"; done
    for i in $(seq $PV_BAR_LEN 49); do printf " "; done
    printf "] %8d bytes\n" $PV_CURRENT_DIFF
    
    printf "  Pass-by-Reference: ["
    for i in $(seq 1 $PR_BAR_LEN); do printf "#"; done
    for i in $(seq $PR_BAR_LEN 49); do printf " "; done
    printf "] %8d bytes\n" $PR_CURRENT_DIFF
    echo ""
fi

# GC usage bar chart
MAX_GC=$(( $PV_GC_DIFF > $PR_GC_DIFF ? $PV_GC_DIFF : $PR_GC_DIFF ))
if [ $MAX_GC -gt 0 ]; then
    PV_GC_BAR=$(( $PV_GC_DIFF * 50 / $MAX_GC ))
    PR_GC_BAR=$(( $PR_GC_DIFF * 50 / $MAX_GC ))
    
    echo "GC Memory Allocation:"
    printf "  Pass-by-Value:     ["
    for i in $(seq 1 $PV_GC_BAR); do printf "#"; done
    for i in $(seq $PV_GC_BAR 49); do printf " "; done
    printf "] %8d bytes\n" $PV_GC_DIFF
    
    printf "  Pass-by-Reference: ["
    for i in $(seq 1 $PR_GC_BAR); do printf "#"; done
    for i in $(seq $PR_GC_BAR 49); do printf " "; done
    printf "] %8d bytes\n" $PR_GC_DIFF
    echo ""
fi

# Time comparison bar chart
MAX_TIME=$(( ${EXEC_TIME_PV:-0} > ${EXEC_TIME_PR:-0} ? ${EXEC_TIME_PV:-0} : ${EXEC_TIME_PR:-0} ))
if [ $MAX_TIME -gt 0 ]; then
    PV_TIME_BAR=$(( ${EXEC_TIME_PV:-0} * 50 / $MAX_TIME ))
    PR_TIME_BAR=$(( ${EXEC_TIME_PR:-0} * 50 / $MAX_TIME ))
    
    echo "Execution Time:"
    printf "  Pass-by-Value:     ["
    for i in $(seq 1 $PV_TIME_BAR); do printf "#"; done
    for i in $(seq $PV_TIME_BAR 49); do printf " "; done
    printf "] %8d ms\n" ${EXEC_TIME_PV:-0}
    
    printf "  Pass-by-Reference: ["
    for i in $(seq 1 $PR_TIME_BAR); do printf "#"; done
    for i in $(seq $PR_TIME_BAR 49); do printf " "; done
    printf "] %8d ms\n" ${EXEC_TIME_PR:-0}
    echo ""
fi

# Final summary with percentages
echo "### PERFORMANCE SUMMARY ###"
echo "┌─────────────────────────────────────────────────────────────┐"
echo "│  METRIC                     │ REDUCTION  │  ACTUAL SAVINGS  │"
echo "├─────────────────────────────────────────────────────────────┤"
printf "│  Current Memory             │  %8s  │  %10d bytes│\n" "${CURRENT_REDUCTION_PERCENT:-N/A}%" $CURRENT_COMPARE
printf "│  Peak Memory                │  %8s  │  %10d bytes│\n" "${PEAK_REDUCTION_PERCENT:-N/A}%" $PEAK_COMPARE
printf "│  Total Memory               │  %8s  │  %10d bytes│\n" "${TOTAL_REDUCTION_PERCENT:-N/A}%" $TOTAL_COMPARE
printf "│  GC Allocation              │  %8s  │  %10d bytes│\n" "${GC_REDUCTION_PERCENT:-N/A}%" $((PV_GC_DIFF - PR_GC_DIFF))
printf "│  Heap Blocks                │  %8s  │ %10d blocks│\n" "${BLOCK_REDUCTION_PERCENT:-N/A}%" $((PV_TOTAL_BLOCKS - PR_TOTAL_BLOCKS))
echo "└─────────────────────────────────────────────────────────────┘"
echo ""

# Additional efficiency metrics
echo "### EFFICIENCY METRICS ###"
echo ""
MEMORY_EFFICIENCY=$(echo "scale=2; $PR_CURRENT_DIFF * 100 / $PV_CURRENT_DIFF" | bc 2>/dev/null || echo "N/A")

echo "Memory Efficiency Ratio:  ${MEMORY_EFFICIENCY}% (Reference uses this % of Value's memory)"
echo ""

