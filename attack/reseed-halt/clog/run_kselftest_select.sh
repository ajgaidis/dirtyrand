TEST_LIST="list.txt"

PROGRESS_LOG="progress.log"

# Clears the progress log
> "$PROGRESS_LOG"

while IFS= read -r test || [ -n "$test" ]; do
	echo "running $test" | tee -a "$PROGRESS_LOG"
	sudo ./run_kselftest.sh --test "$test"
done < "$TEST_LIST"

