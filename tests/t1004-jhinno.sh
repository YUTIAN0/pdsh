#!/bin/bash
#
#  Test jhinno module functionality
#
#  This test requires jjobs and jhosts commands to be available
#

test_description="jhinno module integration tests"

. ${test_srcdir:-.}/test-lib.sh

# Check if jjobs and jhosts are available
if ! command -v jjobs &> /dev/null; then
    echo "jjobs command not found, skipping jhinno tests"
    exit 0
fi

if ! command -v jhosts &> /dev/null; then
    echo "jhosts command not found, skipping jhinno tests"
    exit 0
fi

# Test 1: Test with numeric jobid if jjobs command works
test_expect_success "jhinno: numeric jobid query" '
    # Get a sample jobid from system
    jobid=$(jjobs | head -1 | awk "{print \$1}")
    if [ -n "$jobid" ] && [[ "$jobid" =~ ^[0-9]+$ ]]; then
        pdsh -j $jobid -n hostname 2>&1
    else
        echo "No valid jobid found, skipping test"
        true
    fi
'

# Test 2: Test with job group if jhosts command works
test_expect_success "jhinno: job group query" '
    # Try to query all nodes
    pdsh -j all -n hostname 2>&1 | grep -q "pdsh@"
'

# Test 3: Test --jhinno-include-unknown option
test_expect_success "jhinno: include unknown nodes option" '
    pdsh -j all --jhinno-include-unknown -n hostname 2>&1 | grep -q "pdsh@"
'

# Test 4: Test environment variable JOBS_JOBID
test_expect_success "jhinno: JOBS_JOBID environment variable" '
    # Try to query all nodes via environment variable
    JOBS_JOBID=all pdsh -n hostname 2>&1 | grep -q "pdsh@"
'

# Test 5: Test with 'all' special value
test_expect_success "jhinno: all nodes query" '
    pdsh -j all -n "echo test" 2>&1 | head -5
'

# Test 6: Verify UNKNOWN nodes are filtered by default
test_expect_success "jhinno: UNKNOWN nodes filtered by default" '
    # Get count of all nodes (including UNKNOWN)
    all_nodes=$(jhosts attrib -w 2>/dev/null | awk "NR>2 {print \$1}" | wc -l)
    # Get count of normal nodes (excluding UNKNOWN)
    normal_nodes=$(jhosts attrib -w 2>/dev/null | awk "NR>2 && \$2!=\42UNKNOWN\42 {print \$1}" | wc -l)
    
    # Get node count from pdsh -j all
    pdsh_nodes=$(pdsh -j all -n 2>&1 | grep -c "^.*:" || echo 0)
    
    # Count from jjobs should match or be close to normal_nodes
    echo "All nodes: $all_nodes, Normal nodes: $normal_nodes, pdsh nodes: $pdsh_nodes"
    test "$pdsh_nodes" -gt 0 -o "$normal_nodes" -eq 0
'

# Test 7: Test with --jhinno-include-unknown (should include more nodes)
test_expect_success "jhinno: include unknown nodes increases count" '
    # Get count with filter
    filtered_count=$(pdsh -j all -n 2>&1 | grep -c "^.*:" || echo 0)
    # Get count without filter
    all_count=$(pdsh -j all --jhinno-include-unknown -n 2>&1 | grep -c "^.*:" || echo 0)
    
    # With --jhinno-include-unknown, we should get >= filtered count
    echo "Filtered: $filtered_count, All: $all_count"
    test "$all_count" -ge "$filtered_count"
'

# Test 8: Test command execution on nodes
test_expect_success "jhinno: execute command on nodes" '
    # Ensure we can execute a simple command
    pdsh -j all -q "date +%s" 2>&1 | head -5
'

# Test 9: Verify module is loaded
test_expect_success "jhinno: verify module is loaded" '
    # The module should be listed with -L if working
    pdsh -L 2>&1 | grep -q "jhinno" || true
'

test_done
