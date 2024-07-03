# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -O2

# Libraries
LIBS = -lnuma -lpthread

# Source file
SRC = memcpy_test.cpp

# Output executable
TARGET = memcpy_test

# Default rule
all: $(TARGET)

# Rule to create the executable
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $< $(LIBS) -o $@

# Clean rule to remove the executable
clean:
	rm -f $(TARGET)

# Phony targets to avoid filename conflicts
.PHONY: all clean
