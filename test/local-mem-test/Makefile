# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -Wall -O2

# Libraries
LIBS = -lnuma -lpthread

# Targets
TARGETS = memcpy_test memcpy_test_interleave

# Source files
SRCS = memcpy_test.cpp memcpy_test_interleave.cpp

# Object files
OBJS = $(SRCS:.cpp=.o)

# Default target
all: $(TARGETS)

# Rule to create memcpy_test
memcpy_test: memcpy_test.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

# Rule to create memcpy_test_interleave
memcpy_test_interleave: memcpy_test_interleave.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

# Rule to compile .cpp files to .o files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -f $(OBJS) $(TARGETS)

# Phony targets to avoid filename conflicts
.PHONY: all clean
