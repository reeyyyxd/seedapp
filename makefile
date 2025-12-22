CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2 -pthread -Iinc
LDFLAGS  := -pthread

TARGET   := seed_app

SRCS     := $(wildcard src/*.cpp)
OBJS     := $(patsubst src/%.cpp, build/%.o, $(SRCS))
DEPS     := $(OBJS:.o=.d)

.PHONY: all run clean dirs

all: dirs $(TARGET)

dirs:
	@mkdir -p build

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

build/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

run: all
	./$(TARGET)

clean:
	rm -rf build $(TARGET)

-include $(DEPS)