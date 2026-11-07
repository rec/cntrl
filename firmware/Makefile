.DEFAULT_GOAL := test
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
.PHONY: test
test:
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_buttons.cpp" -o "$(ROOT)/build/test_buttons"
	"$(ROOT)/build/test_buttons"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_input_pipeline.cpp" -o "$(ROOT)/build/test_input_pipeline"
	"$(ROOT)/build/test_input_pipeline" "$(ROOT)/test/show.golden"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_runtime.cpp" -o "$(ROOT)/build/test_runtime"
	"$(ROOT)/build/test_runtime"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_application.cpp" -o "$(ROOT)/build/test_application"
	"$(ROOT)/build/test_application"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_app_layout.cpp" -o "$(ROOT)/build/test_app_layout"
	"$(ROOT)/build/test_app_layout"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_presentation.cpp" -o "$(ROOT)/build/test_presentation"
	"$(ROOT)/build/test_presentation"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_pixel_throb.cpp" -o "$(ROOT)/build/test_pixel_throb"
	"$(ROOT)/build/test_pixel_throb"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_ui.cpp" -o "$(ROOT)/build/test_ui"
	"$(ROOT)/build/test_ui"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_startup_counter.cpp" -o "$(ROOT)/build/test_startup_counter"
	"$(ROOT)/build/test_startup_counter"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_startup_protocol.cpp" -o "$(ROOT)/build/test_startup_protocol"
	"$(ROOT)/build/test_startup_protocol"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_asset_commands.cpp" -o "$(ROOT)/build/test_asset_commands"
	"$(ROOT)/build/test_asset_commands"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_midi_file.cpp" -o "$(ROOT)/build/test_midi_file"
	"$(ROOT)/build/test_midi_file"
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_midi_state.cpp" -o "$(ROOT)/build/test_midi_state"
	"$(ROOT)/build/test_midi_state"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_expression.cpp" -o "$(ROOT)/build/test_expression"
	"$(ROOT)/build/test_expression"
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_usb_session.cpp" -o "$(ROOT)/build/test_usb_session"
	"$(ROOT)/build/test_usb_session"
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I"$(ROOT)/include" -fsanitize=address,undefined "$(ROOT)/test/test_tempo.cpp" -o "$(ROOT)/build/test_tempo"
	"$(ROOT)/build/test_tempo"
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
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		"$(ROOT)/test/animation/test_splash.cpp" -o "$(ROOT)/build/test_animation"
	"$(ROOT)/build/test_animation"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		"$(ROOT)/test/animation/test_runtime.cpp" -o "$(ROOT)/build/test_animation_runtime"
	"$(ROOT)/build/test_animation_runtime" "$(ROOT)/test/show.golden"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -I"$(ROOT)/include" \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		"$(ROOT)/test/test_midi_runtime.cpp" -o "$(ROOT)/build/test_midi_runtime"
	"$(ROOT)/build/test_midi_runtime"
