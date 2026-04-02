#!/bin/bash

# MicroPython Pointers: Complete Test Suite
# Runs all tests in separate interpreter instances

set -e  # Exit on error

MICROPYPLUS="../ports/unix/build-standard/micropython"

echo ""
echo "//**************************************//"
echo "//                                      //"
echo "//   PyPlus: COMPREHENSIVE TEST SUITE   //"
echo "//                                      //"
echo "//**************************************//"
echo ""

# Test 1: Drag Race
echo "Test 1: Drag Race"

$MICROPYPLUS test_drag_race.py
if [ $? -ne 0 ]; then
    echo "Drag race test failed"
    exit 1
fi

# Test 2: Scaling
echo "  Test 2: Memory Efficiency Scaling"

$MICROPYPLUS test_scaling.py
if [ $? -ne 0 ]; then
    echo "Scaling test failed"
    exit 1
fi

# Test 3: Edge Cases
echo "  Test 3: Edge Cases - Type Safety & Error Handling"

$MICROPYPLUS test_edge_cases.py
if [ $? -ne 0 ]; then
    echo "Edge cases test failed"
    exit 1
fi

# Test 4: Memory Leaks
echo "  Test 4: Memory Leak Detection"

$MICROPYPLUS test_memory_leaks.py
if [ $? -ne 0 ]; then
    echo "Memory leak detection test failed"
    exit 1
fi

# Test 5: Nested Pointers
echo "  Test 5: Nested Structures"

$MICROPYPLUS test_nested_pointers.py
if [ $? -ne 0 ]; then
    echo "Nested structures test failed"
    exit 1
fi

# Test 6: Stress Allocation
echo "  Test 6: Concurrent Allocation Stress"

$MICROPYPLUS test_stress_allocation.py
if [ $? -ne 0 ]; then
    echo "Stress allocation test failed"
    exit 1
fi

# Test 7: Pointer Invalidation
echo "  Test 7: Pointer Invalidation"

$MICROPYPLUS test_invalidation.py
if [ $? -ne 0 ]; then
    echo "Pointer invalidation test failed"
    exit 1
fi

# Summary
echo "**************************************************"
echo "ALL TESTS PASSED"
echo "**************************************************"


