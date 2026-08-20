CXX ?= clang++
CXXFLAGS ?= -O3 -std=c++20 -pthread -Wall -Wextra -Werror
DEPFLAGS := -MMD -MP
BUILD_DIR := build

NATIVE_SUITE_SOURCE := approaches/ntuple-rl/native-suite/native.cpp
FAIR_D4_SOURCE := approaches/fair-expectimax/reference/fair-only-depth4.cpp

.PHONY: all native test test-native test-typescript parity experiment research-validate research-doctor clean

all: native

native: $(BUILD_DIR)/native-suite $(BUILD_DIR)/fair-depth4

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/native-suite: $(NATIVE_SUITE_SOURCE) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $< -o $@

$(BUILD_DIR)/fair-depth4: $(FAIR_D4_SOURCE) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $< -o $@

test: research-validate test-typescript test-native parity

test-typescript:
	npm test

test-native: native
	./$(BUILD_DIR)/native-suite --gradient-check
	./$(BUILD_DIR)/native-suite --ntuple-self-test
	./$(BUILD_DIR)/native-suite --ntuple-search-self-test
	./$(BUILD_DIR)/fair-depth4 --self-test

parity: $(BUILD_DIR)/native-suite
	npm run parity

research-validate:
	python3 .agents/skills/million-point-research/scripts/researchctl.py validate
	env PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s research/tests -p 'test_*.py'

research-doctor:
	python3 .agents/skills/million-point-research/scripts/researchctl.py doctor

# Example: make experiment SOURCE=approaches/tree-search/puct/puct.cpp
experiment: | $(BUILD_DIR)
	@test -n "$(SOURCE)" || (echo "Set SOURCE to one C++ experiment" && exit 2)
	$(CXX) $(CXXFLAGS) $(SOURCE) -o $(BUILD_DIR)/experiment

clean:
	rm -rf $(BUILD_DIR)

-include $(BUILD_DIR)/native-suite.d $(BUILD_DIR)/fair-depth4.d
