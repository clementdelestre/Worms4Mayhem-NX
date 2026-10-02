# Desktop build (Linux). Switch: make -f Makefile.switch (in devkitpro/devkita64)
# Incremental: one object per source in obj/ (build/ is Makefile.switch's), header deps via -MMD, parallel by default.
RL := ../third_party/raylib-nx
CXXFLAGS := -std=c++17 -O2 -g -Wall -I$(RL)/src -MMD -MP
LDLIBS := -L$(RL)/out/desktop -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
MAKEFLAGS += -j$(shell nproc)
SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:src/%.cpp=obj/%.o)
LIB := $(filter-out obj/main.o,$(OBJ))
TESTS := sim_check ai_check replay_check mission_check ui_check

worms4nx: $(OBJ)
	$(CXX) $^ -o $@ $(LDLIBS)

obj/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

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
