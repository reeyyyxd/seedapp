CXX = g++
CXXFLAGS = -std=c++11 -Wall -pthread
INCLUDES = -I./inc

SRC_DIR = src
BIN_DIR = bin
INC_DIR = inc

SEED_SOURCES = main.cpp \
               $(SRC_DIR)/clientsocket.cpp \
               $(SRC_DIR)/serversocket.cpp

CLIENT_SOURCES = $(SRC_DIR)/client.cpp \
                 $(SRC_DIR)/clientsocket.cpp

SERVER_SOURCES = $(SRC_DIR)/server.cpp \
                 $(SRC_DIR)/serversocket.cpp

MENU_SOURCES = $(SRC_DIR)/menu.cpp

SEED_TARGET = $(BIN_DIR)/seed
CLIENT_TARGET = $(BIN_DIR)/client
SERVER_TARGET = $(BIN_DIR)/server
MENU_TARGET = $(BIN_DIR)/menu

all: $(BIN_DIR) $(SEED_TARGET)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Build seed app (main target)
$(SEED_TARGET): $(SEED_SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

# Build original client
client: $(BIN_DIR) $(CLIENT_TARGET)

$(CLIENT_TARGET): $(CLIENT_SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

# Build original server
server: $(BIN_DIR) $(SERVER_TARGET)

$(SERVER_TARGET): $(SERVER_SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

# Build menu test
menu: $(BIN_DIR) $(MENU_TARGET)

$(MENU_TARGET): $(MENU_SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

# Build everything
everything: all client server menu

# Clean build artifacts
clean:
	rm -rf $(BIN_DIR)

# Clean and rebuild
rebuild: clean all

# Run seed app
run: $(SEED_TARGET)
	./$(SEED_TARGET)

# Help target
help:
	@echo "Available targets:"
	@echo "  make          - Build seed app (default)"
	@echo "  make all      - Build seed app"
	@echo "  make client   - Build original client"
	@echo "  make server   - Build original server"
	@echo "  make menu     - Build menu test"
	@echo "  make everything - Build all targets"
	@echo "  make clean    - Remove all build artifacts"
	@echo "  make rebuild  - Clean and rebuild seed app"
	@echo "  make run      - Build and run seed app"
	@echo "  make help     - Show this help message"

.PHONY: all client server menu everything clean rebuild run help