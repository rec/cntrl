.DEFAULT_GOAL := test
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
.PHONY: test
test:
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		"$(ROOT)/test/test_midi_bridge.cpp" -o "$(ROOT)/build/test_midi_bridge"
	"$(ROOT)/build/test_midi_bridge"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		"$(ROOT)/test/test_switcher.cpp" -o "$(ROOT)/build/test_switcher"
	"$(ROOT)/build/test_switcher" "$(ROOT)/test/show.golden" "$(ROOT)/test/display.golden"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I"$(ROOT)/include" \
		"$(ROOT)/test/document_server.cpp" -o "$(ROOT)/build/document_server"
