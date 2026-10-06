# Desktop build (Linux). Switch: make -f Makefile.switch (in devkitpro/devkita64)
# Incremental: one object per source in obj/ (build/ is Makefile.switch's), header deps via -MMD, parallel by default.
RL := ../third_party/raylib-nx
LUA := ../third_party/lua-5.0.1
LUA_C := lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lmathlib  # core + base and math libs (W4M opens those two)
CXXFLAGS := -std=c++17 -O2 -g -Wall -I$(RL)/src -I$(LUA)/include -MMD -MP
LDLIBS := -L$(RL)/out/desktop -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
MAKEFLAGS += -j$(shell nproc)
SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:src/%.cpp=obj/%.o) $(LUA_C:%=obj/lua/%.o)
LIB := $(filter-out obj/main.o,$(OBJ))
TESTS := sim_check ai_check replay_check mission_check ui_check

worms4nx: $(OBJ)
	$(CXX) $^ -o $@ $(LDLIBS)

obj/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj/lua/%.o: $(LUA)/src/%.c
	@mkdir -p $(@D)
	$(CC) -O2 -g -I$(LUA)/include -MMD -MP -c $< -o $@

obj/lua/%.o: $(LUA)/src/lib/%.c
	@mkdir -p $(@D)
	$(CC) -O2 -g -I$(LUA)/include -MMD -MP -c $< -o $@

obj/tests/%.o: tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj/bin/%: obj/tests/%.o $(LIB)
	@mkdir -p $(@D)
	$(CXX) $^ -o $@ $(LDLIBS)

# `make <test>` always builds then runs it; ./<test> stays a link to the fresh binary.
.PHONY: clean check $(TESTS)
check: sim_check
$(TESTS): %: obj/bin/%
	ln -sf obj/bin/$@ $@ && ./$@

clean:
	rm -rf obj worms4nx $(TESTS)

-include $(OBJ:.o=.d) $(TESTS:%=obj/tests/%.d)
