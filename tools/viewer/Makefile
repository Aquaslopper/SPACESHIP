# ============================================================================
#  Headless viewer build: the game's own sources, linked against raylib's
#  software renderer (PLATFORM_MEMORY). Separate from the CMake build. All
#  output goes to build/ in this folder, which git ignores.
#
#    ./setup.sh        once: fetch, patch and build the third-party parts
#    make              build build/viewer
#    ./run.sh SCRIPT   render
#
#  REPO is the checkout to look at: by default the one this folder sits in.
# ============================================================================
REPO    ?= $(abspath ../..)
B       := build
DEPS    := $(B)/deps
RAYLIB  := $(DEPS)/raylib-6.0/src
IMGUI   := $(DEPS)/imgui-1.92.7
RLIMGUI := $(DEPS)/rlImGui-Raylib_6_0
JSON    := $(DEPS)/json-3.11.3/single_include

CXX      ?= g++
# -w: this build is for looking, not for compiler diagnostics.
CXXFLAGS := -std=c++17 -O1 -g0 -w -MMD -MP -DSPACESHIP_ASSET_DIR=\"assets\" \
  -I$(REPO)/cpp -I$(JSON) -I$(RAYLIB) -I$(IMGUI) -I$(RLIMGUI) -I$(RLIMGUI)/extras

# The game side is read out of the repository's CMakeLists.txt, so the viewer
# compiles what the client compiles: the spaceship_core list plus ui/Draw.cpp.
# Left out on purpose: cpp/app/main.cpp (the viewer is the main loop here) and
# cpp/audio/AudioDevice.cpp (no sound device).
CORE_SRC := $(shell sed -n '/^add_library(spaceship_core/,/)/p' $(REPO)/CMakeLists.txt \
              | grep -o 'cpp/[A-Za-z0-9_/]*\.cpp')
GAME_SRC := $(CORE_SRC) cpp/ui/Draw.cpp
GAME_OBJS := $(patsubst cpp/%.cpp,$(B)/obj/%.o,$(GAME_SRC))
IMGUI_OBJS := $(addprefix $(B)/obj/imgui/,imgui.o imgui_draw.o imgui_tables.o imgui_widgets.o rlImGui.o)
OBJS := $(GAME_OBJS) $(IMGUI_OBJS) $(B)/obj/viewer.o

all: $(B)/viewer

$(B)/obj/%.o: $(REPO)/cpp/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(B)/obj/imgui/%.o: $(IMGUI)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(B)/obj/imgui/rlImGui.o: $(RLIMGUI)/rlImGui.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(B)/obj/viewer.o: viewer.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --wrap=DrawTextEx lets viewer.cpp record every string the game paints.
$(B)/viewer: $(OBJS) $(RAYLIB)/libraylib.a
	$(CXX) -o $@ $(OBJS) -L$(RAYLIB) -lraylib -lm -lpthread -ldl -Wl,--wrap=DrawTextEx

sources:
	@printf '%s\n' $(GAME_SRC)

clean:
	rm -rf $(B)/obj $(B)/viewer

.PHONY: all sources clean
-include $(OBJS:.o=.d)
