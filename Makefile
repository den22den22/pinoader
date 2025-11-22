# Compiler settings
CXX = g++

# Compiler Flags:
# -std=c++17     : Standard version
# -O2            : Optimization
# -static        : VITAL! Forces static linking of ALL libraries
# -D_WIN32_WINNT : Targets Windows 7 and newer
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -D_WIN32_WINNT=0x0601 -static

# Linker Flags:
# MSYS2 finds libraries automatically.
LDFLAGS = -lssl -lcrypto -lws2_32 -lgdi32 -lcrypt32 -luser32 -ladvapi32

# Project details
TARGET  = pinoader.exe
SOURCES = main.cpp http_client.cpp parser.cpp
OBJECTS = $(SOURCES:.cpp=.o)

# --- Build Rules ---

all: $(TARGET)

$(TARGET): $(OBJECTS)
$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)
@echo "Build successful! Run 'make strip' to reduce file size."

%.o: %.cpp
$(CXX) $(CXXFLAGS) -c -o $@ $<

strip: $(TARGET)
strip -s $(TARGET)
@echo "Executable stripped."

clean:
rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean strip
