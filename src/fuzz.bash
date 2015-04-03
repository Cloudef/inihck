#!/bin/bash
true; while (( $? == 0 )); do s=$RANDOM; echo "iteration $((++i))"; zzuf -x -U 60 -M -1 -s $s valgrind --error-exitcode=1 --leak-check=full ../test/fuzz_test; done; echo "failed on seed: $s"
