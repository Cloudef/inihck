#!/bin/bash
true; while (( $? == 0 )); do s=$RANDOM; zzuf -U 60 -M -1 -s $s valgrind --error-exitcode=1 --leak-check=full ../test/fuzz_test; echo "iteration: $((i++))"; done; echo "failed on seed: $s"
