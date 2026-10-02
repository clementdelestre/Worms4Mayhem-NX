# Desktop build (Linux). Switch: make -f Makefile.switch (in devkitpro/devkita64)
RL := ../third_party/raylib-nx
CXXFLAGS := -std=c++17 -O2 -g -Wall -I$(RL)/src
LDLIBS := -L$(RL)/out/desktop -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
SRC := $(wildcard src/*.cpp)

worms4nx: $(SRC) $(wildcard src/*.h)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDLIBS)

clean:
	rm -f worms4nx sim_check ai_check replay_check mission_check ui_check

check: tests/sim_check.cpp $(filter-out src/main.cpp,$(SRC))
	$(CXX) $(CXXFLAGS) $^ -o sim_check $(LDLIBS) && ./sim_check

ai_check: tests/ai_check.cpp $(filter-out src/main.cpp,$(SRC))
	$(CXX) $(CXXFLAGS) $^ -o ai_check $(LDLIBS) && ./ai_check

replay_check: tests/replay_check.cpp $(filter-out src/main.cpp,$(SRC))
	$(CXX) $(CXXFLAGS) $^ -o replay_check $(LDLIBS) && ./replay_check

mission_check: tests/mission_check.cpp $(filter-out src/main.cpp,$(SRC))
	$(CXX) $(CXXFLAGS) $^ -o mission_check $(LDLIBS) && ./mission_check

ui_check: tests/ui_check.cpp $(filter-out src/main.cpp,$(SRC))
	$(CXX) $(CXXFLAGS) $^ -o ui_check $(LDLIBS) && ./ui_check
