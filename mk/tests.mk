.PHONY: test run-test-args

TEST_ARGS_FILE = tests/program-arguments/dut.elf
TEST_ARGS_EXPECT_FILE = tests/program-arguments/reference.out

test: run-test-args

run-test-args: $(BIN) $(TEST_ARGS_FILE)
	$(Q)result="$$(./$(BIN) $(TEST_ARGS_FILE) -abcd -1234 -boom=1)"; \
	$(PRINTF) "Running program-arguments ... "; \
	expected_output="$$(cat $(TEST_ARGS_EXPECT_FILE))"; \
	if [ "$$result" = "$$expected_output" ]; then \
	$(call notice, [OK]); \
	else \
	$(PRINTF) "Failed.\n"; \
	echo "$$expected_output"; \
	echo "$$result";\
	fi

