# C++ compiler
CXX = g++

# Compiler flags:
# -std=c++17     - Use the C++17 standard (c++14 or c++11 are also fine)
# -Wall          - Enable all warnings (recommended)
# -Wextra        - Enable extra warnings
# -O2            - Optimization level for release builds
# -g             - Include debugging information in the executable
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g

# Linker flags:
# -lssl          - Link against the SSL library
# -lcrypto       - Link against the crypto library (part of OpenSSL)
LDFLAGS = -lssl -lcrypto

# The final executable name
TARGET = pinoader

# List of all source files (.cpp)
SOURCES = main.cpp http_client.cpp parser.cpp

# Automatically generate the list of object files (.o) from the sources
OBJECTS = $(SOURCES:.cpp=.o)

# --- Build Rules ---

# The default target: 'all'. This is executed when you just run 'make'
# It depends on the final target file.
all: $(TARGET)

# The linking rule. Creates the final executable from the object files.
# It depends on all object files.
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)
	@echo "Build of '$@' finished successfully."

# Pattern rule for compilation.
# It tells make how to create a .o file from a corresponding .cpp file.
# $@ is an automatic variable for the target name (e.g., main.o)
# $< is an automatic variable for the first dependency's name (e.g., main.cpp)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# The 'clean' target. Removes all generated files.
clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "Cleanup finished."

# Declare targets that are not actual files.
.PHONY: all clean
