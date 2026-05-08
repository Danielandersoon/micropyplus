#!/bin/bash

set -e  # Exit on error

MICROPYPLUS="../ports/unix/build-standard/micropython"
NUM_RUNS=10  # Number of runs per test

echo ""
echo "//**************************************//"
echo "//                                      //"
echo "//  PyPlus: MULTI-RUN STATISTICAL      //"
echo "//          CACHE ANALYSIS             //"
echo "//                                      //"
echo "//**************************************//"

# Temporary directory for data
DATA_DIR="./pyplus_cache_stats"
mkdir -p "$DATA_DIR"

# Test configurations: (depth, testfile, description)
declare -a TESTS=(
    "3|cache_test.py|3-Level Pointer Caching"
    "6|deep_cache_test.py|6-Level Pointer Caching"
    "12|v_deep_cache_test.py|12-Level Pointer Caching"
    "24|v_v_deep_cache_test.py|24-Level Pointer Caching"
    "96|v_v_v_deep_cache_test.py|96-Level Pointer Caching"
)

run_multiple_tests() {
    local depth=$1
    local testfile=$2
    local description=$3
    
    echo ""
    echo "════════════════════════════════════════════════════════"
    echo "Running [$depth-level]: $description ($NUM_RUNS runs)"
    echo "════════════════════════════════════════════════════════"
    
    local std_file="$DATA_DIR/std_${depth}.txt"
    local ptr_file="$DATA_DIR/ptr_${depth}.txt"
    local speedup_file="$DATA_DIR/speedup_${depth}.txt"
    
    # Clear files
    > "$std_file"
    > "$ptr_file"
    > "$speedup_file"
    
    for run in $(seq 1 $NUM_RUNS); do
        # Run test
        output=$($MICROPYPLUS "$testfile" 2>&1)
        
        # Extract values
        std_time=$(echo "$output" | grep -E "Standard|Standard \(" | grep -oE '[0-9]+' | head -1)
        ptr_time=$(echo "$output" | grep -E "Pointer|Pointer \(" | grep -oE '[0-9]+' | head -1)
        
        if [ -n "$std_time" ] && [ -n "$ptr_time" ]; then
            speedup=$(awk "BEGIN {printf \"%.4f\", $std_time / $ptr_time}")
            
            echo "$std_time" >> "$std_file"
            echo "$ptr_time" >> "$ptr_file"
            echo "$speedup" >> "$speedup_file"
            
            printf "  Run %2d: Std=%8d µs, Ptr=%8d µs, Speedup=%.2fx\n" \
                "$run" "$std_time" "$ptr_time" "$speedup"
        fi
    done
}

# Run all tests
for test_config in "${TESTS[@]}"; do
    IFS='|' read -r depth testfile description <<< "$test_config"
    run_multiple_tests "$depth" "$testfile" "$description"
done

# Generate Statistical Report
echo ""
echo "════════════════════════════════════════════════════════"
echo "GENERATING DETAILED STATISTICAL REPORT"
echo "════════════════════════════════════════════════════════"
echo ""

# Function to calculate and display statistics for a single data file
calculate_stats() {
    local label=$1
    local data_file=$2
    
    if [ ! -f "$data_file" ] || [ ! -s "$data_file" ]; then
        echo "  $label: No data"
        return
    fi
    
    awk -v label="$label" '
    {
        count++
        val = $1
        sum += val
        values[count] = val
        if (count == 1 || val < min) min = val
        if (count == 1 || val > max) max = val
    }
    END {
        if (count == 0) exit
        
        # Mean
        mean = sum / count
        
        # Variance and Standard Deviation
        var_sum = 0
        for (i = 1; i <= count; i++) {
            var_sum += (values[i] - mean) ^ 2
        }
        variance = var_sum / count
        stddev = sqrt(variance)
        
        # Coefficient of Variation
        cv = (mean > 0) ? (stddev / mean) * 100 : 0
        
        # 95% Confidence Interval
        se = stddev / sqrt(count)            # Standard error
        ci_margin = 1.96 * se                # 95% CI margin
        ci_lower = mean - ci_margin
        ci_upper = mean + ci_margin
        
        # Median and Percentiles
        for (i = 1; i <= count; i++) {
            sorted[i] = values[i]
        }
        
        # Simple bubble sort
        for (i = 1; i < count; i++) {
            for (j = i + 1; j <= count; j++) {
                if (sorted[i] > sorted[j]) {
                    temp = sorted[i]
                    sorted[i] = sorted[j]
                    sorted[j] = temp
                }
            }
        }
        
        # Median
        if (count % 2 == 1) {
            median = sorted[(count + 1) / 2]
        } else {
            median = (sorted[count / 2] + sorted[count / 2 + 1]) / 2
        }
        
        # 95th percentile
        p95_idx = int(count * 0.95)
        if (p95_idx < 1) p95_idx = 1
        if (p95_idx > count) p95_idx = count
        p95 = sorted[p95_idx]
        
        # Output statistics
        printf "  %s (n=%d):\n", label, count
        printf "    Mean:     %15.2f µs\n", mean
        printf "    Median:   %15.2f µs\n", median
        printf "    Std Dev:  %15.2f µs (CV: %.2f%%)\n", stddev, cv
        printf "    Min/Max:  %15.2f / %.2f µs\n", min, max
        printf "    95th %%ile: %15.2f µs\n", p95
        printf "    95%% CI:   [%.2f, %.2f] µs\n", ci_lower, ci_upper
    }
    ' "$data_file"
}

# Display statistics for each depth
for test_config in "${TESTS[@]}"; do
    IFS='|' read -r depth testfile description <<< "$test_config"
    
    std_file="$DATA_DIR/std_${depth}.txt"
    ptr_file="$DATA_DIR/ptr_${depth}.txt"
    speedup_file="$DATA_DIR/speedup_${depth}.txt"
    
    if [ -f "$std_file" ] && [ -s "$std_file" ]; then
        echo "┌─────────────────────────────────────────────────────┐"
        printf "│ DEPTH %d - %s\n" "$depth" "$description"
        echo "└─────────────────────────────────────────────────────┘"
        echo ""
        
        calculate_stats "Standard Access" "$std_file"
        echo ""
        calculate_stats "Pointer Cached Access" "$ptr_file"
        echo ""
        calculate_stats "Speedup Factor" "$speedup_file"
        echo ""
    fi
done

# Cross-depth comparison table
echo "════════════════════════════════════════════════════════"
echo "SUMMARY TABLE - MEAN ± SD (CV) - ALL DEPTHS"
echo "════════════════════════════════════════════════════════"
echo ""
echo "┌───────┬──────────────────────┬──────────────────────┬────────────────┐"
echo "│ Depth │  Standard Access     │  Pointer Cached      │  Speedup Factor│"
echo "├───────┼──────────────────────┼──────────────────────┼────────────────┤"

for test_config in "${TESTS[@]}"; do
    IFS='|' read -r depth testfile description <<< "$test_config"
    
    std_file="$DATA_DIR/std_${depth}.txt"
    ptr_file="$DATA_DIR/ptr_${depth}.txt"
    speedup_file="$DATA_DIR/speedup_${depth}.txt"
    
    if [ -f "$std_file" ] && [ -s "$std_file" ]; then
        # Standard stats
        std_stats=$(awk '
        {
            count++
            sum += $1
            values[count] = $1
        }
        END {
            mean = sum / count
            for (i = 1; i <= count; i++) {
                var_sum += (values[i] - mean) ^ 2
            }
            sd = sqrt(var_sum / count)
            cv = (sd / mean) * 100
            printf "%d±%d(%.1f%%)", mean, sd, cv
        }
        ' "$std_file")
        
        # Pointer stats
        ptr_stats=$(awk '
        {
            count++
            sum += $1
            values[count] = $1
        }
        END {
            mean = sum / count
            for (i = 1; i <= count; i++) {
                var_sum += (values[i] - mean) ^ 2
            }
            sd = sqrt(var_sum / count)
            cv = (sd / mean) * 100
            printf "%d±%d(%.1f%%)", mean, sd, cv
        }
        ' "$ptr_file")
        
        # Speedup stats
        speedup_stats=$(awk '
        {
            count++
            sum += $1
            values[count] = $1
        }
        END {
            mean = sum / count
            for (i = 1; i <= count; i++) {
                var_sum += (values[i] - mean) ^ 2
            }
            sd = sqrt(var_sum / count)
            cv = (sd / mean) * 100
            printf "%.2f±%.3f(%.1f%%)", mean, sd, cv
        }
        ' "$speedup_file")
        
        printf "│ %3d   │ %20s │ %20s │ %14s │\n" "$depth" "$std_stats" "$ptr_stats" "$speedup_stats"
    fi
done

echo "└───────┴──────────────────────┴──────────────────────┴────────────────┘"
echo ""
echo "Note: Format is Mean ± StdDev (Coefficient of Variation)"
echo ""

echo ""
echo "════════════════════════════════════════════════════════"
echo "Analysis complete. Raw data in: $DATA_DIR"
echo "════════════════════════════════════════════════════════"
