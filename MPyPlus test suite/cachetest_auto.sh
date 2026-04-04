#!/bin/bash

set -e  # Exit on error

MICROPYPLUS="../ports/unix/build-standard/micropython"

echo ""
echo "//**************************************//"
echo "//                                      //"
echo "//       PyPlus: CACHE TESTS            //"
echo "//                                      //"
echo "//**************************************//"

# Test 1: 3 level pointer caching
echo "Test 1: 3-Level Pointer Caching"
$MICROPYPLUS cache_test.py

if [ $? -ne 0 ]; then
    echo "3-level pointer caching test failed"
    exit 1
fi

# Test 2: 6 level pointer caching
echo "Test 2: 6-Level Pointer Caching"
$MICROPYPLUS deep_cache_test.py

if [ $? -ne 0 ]; then
    echo "6-level pointer caching test failed"
    exit 1
fi

# Test 3: 12 level pointer caching
echo "Test 3: 12-Level Pointer Caching"
$MICROPYPLUS v_deep_cache_test.py

if [ $? -ne 0 ]; then
    echo "12-level pointer caching test failed"
    exit 1
fi

# Test 4: 24 level pointer caching
echo "Test 4: 24-Level Pointer Caching"
$MICROPYPLUS v_v_deep_cache_test.py

if [ $? -ne 0 ]; then
    echo "24-level pointer caching test failed"
    exit 1
fi

# Test 5: 96 level pointer caching
echo "Test 5: 96-Level Pointer Caching"
$MICROPYPLUS v_v_v_deep_cache_test.py

if [ $? -ne 0 ]; then
    echo "96-level pointer caching test failed"
    exit 1
fi


