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
PR_BEFORE=$(echo "$OUTPUT" | grep -A 10 "TEST 2: PASS-BY-REFERENCE (DIRECT ACCESS)" | grep "mem: total=" | head -1 | sed -n 's/.*total=\([0-9]*\), current=\([0-9]*\), peak=\([0-9]*\).*/\1 \2 \3/p')
PR_AFTER=$(echo "$OUTPUT" | grep -A 20 "TEST 2: PASS-BY-REFERENCE (DIRECT ACCESS)" | grep "mem: total=" | tail -1 | sed -n 's/.*total=\([0-9]*\), current=\([0-9]*\), peak=\([0-9]*\).*/\1 \2 \3/p')

# Extract Pass-by-Assignment memory values
PA_BEFORE=$(echo "$OUTPUT" | grep -A 10 "TEST 3: PASS-BY-ASSIGNMENT (PYTHON DEFAULT)" | grep "mem: total=" | head -1 | sed -n 's/.*total=\([0-9]*\), current=\([0-9]*\), peak=\([0-9]*\).*/\1 \2 \3/p')
PA_AFTER=$(echo "$OUTPUT" | grep -A 20 "TEST 3: PASS-BY-ASSIGNMENT (PYTHON DEFAULT)" | grep "mem: total=" | tail -1 | sed -n 's/.*total=\([0-9]*\), current=\([0-9]*\), peak=\([0-9]*\).*/\1 \2 \3/p')

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

PA_BEFORE_TOTAL=$(echo $PA_BEFORE | cut -d' ' -f1)
PA_BEFORE_CURRENT=$(echo $PA_BEFORE | cut -d' ' -f2)
PA_BEFORE_PEAK=$(echo $PA_BEFORE | cut -d' ' -f3)
PA_AFTER_TOTAL=$(echo $PA_AFTER | cut -d' ' -f1)
PA_AFTER_CURRENT=$(echo $PA_AFTER | cut -d' ' -f2)
PA_AFTER_PEAK=$(echo $PA_AFTER | cut -d' ' -f3)

# Calculate differences
PV_CURRENT_DIFF=$((PV_AFTER_CURRENT - PV_BEFORE_CURRENT))
PV_PEAK_DIFF=$((PV_AFTER_PEAK - PV_BEFORE_PEAK))
PV_TOTAL_DIFF=$((PV_AFTER_TOTAL - PV_BEFORE_TOTAL))

PR_CURRENT_DIFF=$((PR_AFTER_CURRENT - PR_BEFORE_CURRENT))
PR_PEAK_DIFF=$((PR_AFTER_PEAK - PR_BEFORE_PEAK))
PR_TOTAL_DIFF=$((PR_AFTER_TOTAL - PR_BEFORE_TOTAL))

PA_CURRENT_DIFF=$((PA_AFTER_CURRENT - PA_BEFORE_CURRENT))
PA_PEAK_DIFF=$((PA_AFTER_PEAK - PA_BEFORE_PEAK))
PA_TOTAL_DIFF=$((PA_AFTER_TOTAL - PA_BEFORE_TOTAL))

# Compare the approaches
CURRENT_COMPARE_PV_PR=$((PV_CURRENT_DIFF - PR_CURRENT_DIFF))
PEAK_COMPARE_PV_PR=$((PV_PEAK_DIFF - PR_PEAK_DIFF))
TOTAL_COMPARE_PV_PR=$((PV_TOTAL_DIFF - PR_TOTAL_DIFF))

CURRENT_COMPARE_PV_PA=$((PV_CURRENT_DIFF - PA_CURRENT_DIFF))
PEAK_COMPARE_PV_PA=$((PV_PEAK_DIFF - PA_PEAK_DIFF))
TOTAL_COMPARE_PV_PA=$((PV_TOTAL_DIFF - PA_TOTAL_DIFF))

CURRENT_COMPARE_PR_PA=$((PR_CURRENT_DIFF - PA_CURRENT_DIFF))
PEAK_COMPARE_PR_PA=$((PR_PEAK_DIFF - PA_PEAK_DIFF))
TOTAL_COMPARE_PR_PA=$((PR_TOTAL_DIFF - PA_TOTAL_DIFF))

# Extract GC information
echo "### GARBAGE COLLECTOR ANALYSIS ###"
echo ""

# Extract GC values for Pass-by-Value
PV_GC_BEFORE=$(echo "$OUTPUT" | grep -A 10 "TEST 1: PASS-BY-VALUE VERSION" | grep "GC: total:" | head -1)
PV_GC_AFTER=$(echo "$OUTPUT" | grep -A 20 "TEST 1: PASS-BY-VALUE VERSION" | grep "GC: total:" | tail -1)

# Extract GC values for Pass-by-Reference
PR_GC_BEFORE=$(echo "$OUTPUT" | grep -A 10 "TEST 2: PASS-BY-REFERENCE (DIRECT ACCESS)" | grep "GC: total:" | head -1)
PR_GC_AFTER=$(echo "$OUTPUT" | grep -A 20 "TEST 2: PASS-BY-REFERENCE (DIRECT ACCESS)" | grep "GC: total:" | tail -1)

# Extract GC values for Pass-by-Assignment
PA_GC_BEFORE=$(echo "$OUTPUT" | grep -A 10 "TEST 3: PASS-BY-ASSIGNMENT (PYTHON DEFAULT)" | grep "GC: total:" | head -1)
PA_GC_AFTER=$(echo "$OUTPUT" | grep -A 20 "TEST 3: PASS-BY-ASSIGNMENT (PYTHON DEFAULT)" | grep "GC: total:" | tail -1)

# Parse GC values
PV_GC_BEFORE_USED=$(echo $PV_GC_BEFORE | sed -n 's/.*used: \([0-9]*\),.*/\1/p')
PV_GC_AFTER_USED=$(echo $PV_GC_AFTER | sed -n 's/.*used: \([0-9]*\),.*/\1/p')
PR_GC_BEFORE_USED=$(echo $PR_GC_BEFORE | sed -n 's/.*used: \([0-9]*\),.*/\1/p')
PR_GC_AFTER_USED=$(echo $PR_GC_AFTER | sed -n 's/.*used: \([0-9]*\),.*/\1/p')
PA_GC_BEFORE_USED=$(echo $PA_GC_BEFORE | sed -n 's/.*used: \([0-9]*\),.*/\1/p')
PA_GC_AFTER_USED=$(echo $PA_GC_AFTER | sed -n 's/.*used: \([0-9]*\),.*/\1/p')

PV_GC_DIFF=$((PV_GC_AFTER_USED - PV_GC_BEFORE_USED))
PR_GC_DIFF=$((PR_GC_AFTER_USED - PR_GC_BEFORE_USED))
PA_GC_DIFF=$((PA_GC_AFTER_USED - PA_GC_BEFORE_USED))

# Extract block information
PV_BLOCKS_BEFORE=$(echo "$OUTPUT" | grep -A 10 "TEST 1: PASS-BY-VALUE VERSION" | grep "No. of 1-blocks:" | head -1)
PV_BLOCKS_AFTER=$(echo "$OUTPUT" | grep -A 20 "TEST 1: PASS-BY-VALUE VERSION" | grep "No. of 1-blocks:" | tail -1)
PR_BLOCKS_BEFORE=$(echo "$OUTPUT" | grep -A 10 "TEST 2: PASS-BY-REFERENCE (DIRECT ACCESS)" | grep "No. of 1-blocks:" | head -1)
PR_BLOCKS_AFTER=$(echo "$OUTPUT" | grep -A 20 "TEST 2: PASS-BY-REFERENCE (DIRECT ACCESS)" | grep "No. of 1-blocks:" | tail -1)
PA_BLOCKS_BEFORE=$(echo "$OUTPUT" | grep -A 10 "TEST 3: PASS-BY-ASSIGNMENT (PYTHON DEFAULT)" | grep "No. of 1-blocks:" | head -1)
PA_BLOCKS_AFTER=$(echo "$OUTPUT" | grep -A 20 "TEST 3: PASS-BY-ASSIGNMENT (PYTHON DEFAULT)" | grep "No. of 1-blocks:" | tail -1)

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
echo "PASS-BY-ASSIGNMENT MEMORY CHANGES:"
echo "  Current memory: +$PA_CURRENT_DIFF bytes"
echo "  Peak memory:    +$PA_PEAK_DIFF bytes"
echo "  Total memory:   +$PA_TOTAL_DIFF bytes"
echo "  GC used:        +$PA_GC_DIFF bytes"
echo ""
echo "----------------------------------------"
echo "MEMORY USAGE COMPARISON (Value vs Reference):"
echo "  Current memory difference: $CURRENT_COMPARE_PV_PR bytes"
echo "  Peak memory difference:    $PEAK_COMPARE_PV_PR bytes"
echo "  Total memory difference:   $TOTAL_COMPARE_PV_PR bytes"
echo "  GC usage difference:       $((PV_GC_DIFF - PR_GC_DIFF)) bytes"
echo ""
echo "MEMORY USAGE COMPARISON (Value vs Assignment):"
echo "  Current memory difference: $CURRENT_COMPARE_PV_PA bytes"
echo "  Peak memory difference:    $PEAK_COMPARE_PV_PA bytes"
echo "  Total memory difference:   $TOTAL_COMPARE_PV_PA bytes"
echo "  GC usage difference:       $((PV_GC_DIFF - PA_GC_DIFF)) bytes"
echo ""
echo "MEMORY USAGE COMPARISON (Reference vs Assignment):"
echo "  Current memory difference: $CURRENT_COMPARE_PR_PA bytes"
echo "  Peak memory difference:    $PEAK_COMPARE_PR_PA bytes"
echo "  Total memory difference:   $TOTAL_COMPARE_PR_PA bytes"
echo "  GC usage difference:       $((PR_GC_DIFF - PA_GC_DIFF)) bytes"
echo ""

# Performance comparison from output
EXEC_TIME_PV=$(echo "$OUTPUT" | grep -A 10 "TEST 1: PASS-BY-VALUE VERSION" | grep "Total time:" | sed -n 's/.*Total time: \([0-9]*\) ms.*/\1/p')
EXEC_TIME_PR=$(echo "$OUTPUT" | grep -A 10 "TEST 2: PASS-BY-REFERENCE (DIRECT ACCESS)" | grep "Total time:" | sed -n 's/.*Total time: \([0-9]*\) ms.*/\1/p')
EXEC_TIME_PA=$(echo "$OUTPUT" | grep -A 10 "TEST 3: PASS-BY-ASSIGNMENT (PYTHON DEFAULT)" | grep "Total time:" | sed -n 's/.*Total time: \([0-9]*\) ms.*/\1/p')

echo "### EXECUTION TIME ANALYSIS ###"
echo "  Pass-by-Value:           ${EXEC_TIME_PV:-N/A} ms"
echo "  Pass-by-Reference:       ${EXEC_TIME_PR:-N/A} ms"
echo "  Pass-by-Assignment:      ${EXEC_TIME_PA:-N/A} ms"
if [ ${EXEC_TIME_PV:-0} -ne 0 ] && [ ${EXEC_TIME_PR:-0} -ne 0 ]; then
    echo "  Value vs Reference time: $((EXEC_TIME_PV - EXEC_TIME_PR)) ms faster"
fi
if [ ${EXEC_TIME_PV:-0} -ne 0 ] && [ ${EXEC_TIME_PA:-0} -ne 0 ]; then
    echo "  Value vs Assignment time: $((EXEC_TIME_PV - EXEC_TIME_PA)) ms faster"
fi
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

PA_1BLK_BEFORE=$(echo $PA_BLOCKS_BEFORE | sed -n 's/.*1-blocks: \([0-9]*\),.*/\1/p')
PA_1BLK_AFTER=$(echo $PA_BLOCKS_AFTER | sed -n 's/.*1-blocks: \([0-9]*\),.*/\1/p')
PA_2BLK_BEFORE=$(echo $PA_BLOCKS_BEFORE | sed -n 's/.*2-blocks: \([0-9]*\),.*/\1/p')
PA_2BLK_AFTER=$(echo $PA_BLOCKS_AFTER | sed -n 's/.*2-blocks: \([0-9]*\),.*/\1/p')

echo "Pass-by-Value block changes:"
echo "  1-blocks: +$((PV_1BLK_AFTER - PV_1BLK_BEFORE))"
echo "  2-blocks: +$((PV_2BLK_AFTER - PV_2BLK_BEFORE))"
echo ""
echo "Pass-by-Reference block changes:"
echo "  1-blocks: +$((PR_1BLK_AFTER - PR_1BLK_BEFORE))"
echo "  2-blocks: +$((PR_2BLK_AFTER - PR_2BLK_BEFORE))"
echo ""
echo "Pass-by-Assignment block changes:"
echo "  1-blocks: +$((PA_1BLK_AFTER - PA_1BLK_BEFORE))"
echo "  2-blocks: +$((PA_2BLK_AFTER - PA_2BLK_BEFORE))"
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
PA_TOTAL_BLOCKS=$(( (PA_1BLK_AFTER - PA_1BLK_BEFORE) + (PA_2BLK_AFTER - PA_2BLK_BEFORE) ))

if [ $PV_TOTAL_BLOCKS -ne 0 ]; then
    BLOCK_REDUCTION_PERCENT=$(echo "scale=2; ($PV_TOTAL_BLOCKS - $PR_TOTAL_BLOCKS) * 100 / $PV_TOTAL_BLOCKS" | bc)
    echo "Heap Block Reduction:      ${BLOCK_REDUCTION_PERCENT}% fewer blocks allocated"
else
    echo "Heap Block Reduction:      N/A (no baseline)"
fi

# Time reduction percentage (Value vs Reference)
if [ ${EXEC_TIME_PV:-0} -ne 0 ]; then
    TIME_REDUCTION_PV_PR=$(echo "scale=2; (${EXEC_TIME_PV:-0} - ${EXEC_TIME_PR:-0}) * 100 / ${EXEC_TIME_PV:-0}" | bc)
    echo "Execution Time Reduction (Value vs Reference): ${TIME_REDUCTION_PV_PR}% faster"
else
    echo "Execution Time Reduction (Value vs Reference): N/A (no baseline)"
fi

# Time reduction percentage (Value vs Assignment)
if [ ${EXEC_TIME_PV:-0} -ne 0 ]; then
    TIME_REDUCTION_PV_PA=$(echo "scale=2; (${EXEC_TIME_PV:-0} - ${EXEC_TIME_PA:-0}) * 100 / ${EXEC_TIME_PV:-0}" | bc)
    echo "Execution Time Reduction (Value vs Assignment): ${TIME_REDUCTION_PV_PA}% faster"
else
    echo "Execution Time Reduction (Value vs Assignment): N/A (no baseline)"
fi

echo ""

# Visual bar chart representation
echo "### VISUAL COMPARISON ###"
echo ""

# Calculate bar lengths (max 50 chars)
MAX_MEM=$(( $PV_CURRENT_DIFF > $PR_CURRENT_DIFF ? $PV_CURRENT_DIFF : $PR_CURRENT_DIFF ))
MAX_MEM=$(( $MAX_MEM > $PA_CURRENT_DIFF ? $MAX_MEM : $PA_CURRENT_DIFF ))
if [ $MAX_MEM -gt 0 ]; then
    PV_BAR_LEN=$(( $PV_CURRENT_DIFF * 50 / $MAX_MEM ))
    PR_BAR_LEN=$(( $PR_CURRENT_DIFF * 50 / $MAX_MEM ))
    PA_BAR_LEN=$(( $PA_CURRENT_DIFF * 50 / $MAX_MEM ))
    
    echo "Current Memory Usage Increase:"
    printf "  Pass-by-Value:       ["
    for i in $(seq 1 $PV_BAR_LEN); do printf "#"; done
    for i in $(seq $PV_BAR_LEN 49); do printf " "; done
    printf "] %8d bytes\n" $PV_CURRENT_DIFF
    
    printf "  Pass-by-Reference:   ["
    for i in $(seq 1 $PR_BAR_LEN); do printf "#"; done
    for i in $(seq $PR_BAR_LEN 49); do printf " "; done
    printf "] %8d bytes\n" $PR_CURRENT_DIFF
    
    printf "  Pass-by-Assignment:  ["
    for i in $(seq 1 $PA_BAR_LEN); do printf "#"; done
    for i in $(seq $PA_BAR_LEN 49); do printf " "; done
    printf "] %8d bytes\n" $PA_CURRENT_DIFF
    echo ""
fi

# GC usage bar chart
MAX_GC=$(( $PV_GC_DIFF > $PR_GC_DIFF ? $PV_GC_DIFF : $PR_GC_DIFF ))
MAX_GC=$(( $MAX_GC > $PA_GC_DIFF ? $MAX_GC : $PA_GC_DIFF ))
if [ $MAX_GC -gt 0 ]; then
    PV_GC_BAR=$(( $PV_GC_DIFF * 50 / $MAX_GC ))
    PR_GC_BAR=$(( $PR_GC_DIFF * 50 / $MAX_GC ))
    PA_GC_BAR=$(( $PA_GC_DIFF * 50 / $MAX_GC ))
    
    echo "GC Memory Allocation:"
    printf "  Pass-by-Value:       ["
    for i in $(seq 1 $PV_GC_BAR); do printf "#"; done
    for i in $(seq $PV_GC_BAR 49); do printf " "; done
    printf "] %8d bytes\n" $PV_GC_DIFF
    
    printf "  Pass-by-Reference:   ["
    for i in $(seq 1 $PR_GC_BAR); do printf "#"; done
    for i in $(seq $PR_GC_BAR 49); do printf " "; done
    printf "] %8d bytes\n" $PR_GC_DIFF
    
    printf "  Pass-by-Assignment:  ["
    for i in $(seq 1 $PA_GC_BAR); do printf "#"; done
    for i in $(seq $PA_GC_BAR 49); do printf " "; done
    printf "] %8d bytes\n" $PA_GC_DIFF
    echo ""
fi

# Time comparison bar chart
MAX_TIME=$(( ${EXEC_TIME_PV:-0} > ${EXEC_TIME_PR:-0} ? ${EXEC_TIME_PV:-0} : ${EXEC_TIME_PR:-0} ))
MAX_TIME=$(( $MAX_TIME > ${EXEC_TIME_PA:-0} ? $MAX_TIME : ${EXEC_TIME_PA:-0} ))
if [ $MAX_TIME -gt 0 ]; then
    PV_TIME_BAR=$(( ${EXEC_TIME_PV:-0} * 50 / $MAX_TIME ))
    PR_TIME_BAR=$(( ${EXEC_TIME_PR:-0} * 50 / $MAX_TIME ))
    PA_TIME_BAR=$(( ${EXEC_TIME_PA:-0} * 50 / $MAX_TIME ))
    
    echo "Execution Time:"
    printf "  Pass-by-Value:       ["
    for i in $(seq 1 $PV_TIME_BAR); do printf "#"; done
    for i in $(seq $PV_TIME_BAR 49); do printf " "; done
    printf "] %8d ms\n" ${EXEC_TIME_PV:-0}
    
    printf "  Pass-by-Reference:   ["
    for i in $(seq 1 $PR_TIME_BAR); do printf "#"; done
    for i in $(seq $PR_TIME_BAR 49); do printf " "; done
    printf "] %8d ms\n" ${EXEC_TIME_PR:-0}
    
    printf "  Pass-by-Assignment:  ["
    for i in $(seq 1 $PA_TIME_BAR); do printf "#"; done
    for i in $(seq $PA_TIME_BAR 49); do printf " "; done
    printf "] %8d ms\n" ${EXEC_TIME_PA:-0}
    echo ""
fi

# Final summary with percentages
echo "### PERFORMANCE SUMMARY ###"
echo "┌──────────────────────────────────────────────────────────────────────────┐"
echo "│              METRIC              │ VALUE vs REF │ VALUE vs ASSIGN │"
echo "├──────────────────────────────────────────────────────────────────────────┤"
printf "│  Current Memory (bytes)          │  %10d  │     %10d    │\n" $CURRENT_COMPARE_PV_PR $CURRENT_COMPARE_PV_PA
printf "│  Peak Memory (bytes)             │  %10d  │     %10d    │\n" $PEAK_COMPARE_PV_PR $PEAK_COMPARE_PV_PA
printf "│  Total Memory (bytes)            │  %10d  │     %10d    │\n" $TOTAL_COMPARE_PV_PR $TOTAL_COMPARE_PV_PA
printf "│  GC Allocation (bytes)           │  %10d  │     %10d    │\n" $((PV_GC_DIFF - PR_GC_DIFF)) $((PV_GC_DIFF - PA_GC_DIFF))
printf "│  Heap Blocks                     │  %10d  │     %10d    │\n" $((PV_TOTAL_BLOCKS - PR_TOTAL_BLOCKS)) $((PV_TOTAL_BLOCKS - PA_TOTAL_BLOCKS))
echo "└──────────────────────────────────────────────────────────────────────────┘"
echo ""

# Additional efficiency metrics
echo "### EFFICIENCY METRICS ###"
echo ""
if [ $PV_CURRENT_DIFF -ne 0 ]; then
    MEMORY_EFFICIENCY_PR=$(echo "scale=2; $PR_CURRENT_DIFF * 100 / $PV_CURRENT_DIFF" | bc 2>/dev/null || echo "N/A")
    MEMORY_EFFICIENCY_PA=$(echo "scale=2; $PA_CURRENT_DIFF * 100 / $PV_CURRENT_DIFF" | bc 2>/dev/null || echo "N/A")
    echo "Pass-by-Reference uses ${MEMORY_EFFICIENCY_PR}% of Pass-by-Value's memory"
    echo "Pass-by-Assignment uses ${MEMORY_EFFICIENCY_PA}% of Pass-by-Value's memory"
else
    echo "Memory Efficiency Ratio: N/A (no baseline)"
fi
echo ""

