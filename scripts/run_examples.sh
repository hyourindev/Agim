#!/bin/bash
#
# Run all Tofu example programs and measure execution time
# Usage: cd build && ../scripts/run_examples.sh
#

set -e

TOFU="./tofu"
PROGRAMS_DIR="../examples"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo "========================================"
echo "    TOFU PROGRAM BENCHMARK SUITE"
echo "========================================"
echo ""

# Check if tofu binary exists
if [ ! -f "$TOFU" ]; then
    echo -e "${RED}Error: $TOFU not found. Run from build directory.${NC}"
    exit 1
fi

# Count programs
TOTAL=$(ls -1 "$PROGRAMS_DIR"/*.firm 2>/dev/null | wc -l)
echo "Found $TOTAL programs to run"
echo ""

# Stats
PASSED=0
FAILED=0
TOTAL_TIME=0

# Results array
declare -a RESULTS

echo "Running programs..."
echo "----------------------------------------"
printf "%-35s %10s %8s\n" "PROGRAM" "TIME" "STATUS"
echo "----------------------------------------"

for file in "$PROGRAMS_DIR"/*.firm; do
    name=$(basename "$file")

    # Time the execution
    START=$(date +%s.%N)

    # Run with timeout (5 seconds max per program)
    if timeout 5s "$TOFU" "$file" > /tmp/tofu_output.txt 2>&1; then
        STATUS="OK"
        ((PASSED++))
        COLOR=$GREEN
    else
        EXIT_CODE=$?
        if [ $EXIT_CODE -eq 124 ]; then
            STATUS="TIMEOUT"
        else
            STATUS="FAIL"
        fi
        ((FAILED++))
        COLOR=$RED
    fi

    END=$(date +%s.%N)
    TIME=$(echo "$END - $START" | bc)
    TIME_MS=$(echo "$TIME * 1000" | bc | cut -d'.' -f1)
    TOTAL_TIME=$(echo "$TOTAL_TIME + $TIME" | bc)

    # Store result
    RESULTS+=("$name|$TIME_MS|$STATUS")

    # Print result
    printf "%-35s %8s ms %8s\n" "$name" "$TIME_MS" "[$STATUS]"
done

echo "----------------------------------------"
echo ""

# Summary
echo "========================================"
echo "                SUMMARY"
echo "========================================"
echo ""
printf "${GREEN}Passed:${NC}  %d\n" "$PASSED"
printf "${RED}Failed:${NC}  %d\n" "$FAILED"
echo "----------------------------------------"
printf "Total:   %d programs\n" "$TOTAL"
printf "Time:    %.2f seconds\n" "$TOTAL_TIME"
echo ""

# Top 5 slowest
echo "Top 5 Slowest Programs:"
echo "----------------------------------------"
for result in "${RESULTS[@]}"; do
    echo "$result"
done | sort -t'|' -k2 -rn | head -5 | while IFS='|' read name time status; do
    printf "  %-30s %5s ms\n" "$name" "$time"
done

echo ""
echo "========================================"
echo "Benchmark complete!"
echo "========================================"
