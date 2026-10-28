.DEFAULT_GOAL := test
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
.PHONY: test
test:
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_patch_executor.cpp" -o "$(ROOT)/build/test_patch_executor"
	"$(ROOT)/build/test_patch_executor" "$(ROOT)/test/actions.golden"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_patch_sources.cpp" -o "$(ROOT)/build/test_patch_sources"
	"$(ROOT)/build/test_patch_sources" "$(ROOT)/test/actions.golden"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_actions.cpp" -o "$(ROOT)/build/test_actions"
	"$(ROOT)/build/test_actions" "$(ROOT)/test/actions.golden"
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		"$(ROOT)/test/test_panic.cpp" -o "$(ROOT)/build/test_panic"
	"$(ROOT)/build/test_panic"
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" \
		-fsanitize=address,undefined "$(ROOT)/test/test_songs.cpp" -o "$(ROOT)/build/test_songs"
	"$(ROOT)/build/test_songs"
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		"$(ROOT)/test/test_history_steps.cpp" -o "$(ROOT)/build/test_history_steps"
	"$(ROOT)/build/test_history_steps"
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		"$(ROOT)/test/test_midi_interface.cpp" -o "$(ROOT)/build/test_midi_interface"
	"$(ROOT)/build/test_midi_interface"
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		"$(ROOT)/test/test_cable_test.cpp" -o "$(ROOT)/build/test_cable_test"
	"$(ROOT)/build/test_cable_test"
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
