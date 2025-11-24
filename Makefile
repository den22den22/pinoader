CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -D_WIN32_WINNT=0x0601 -static
LDFLAGS = -lssl -lcrypto -lws2_32 -lgdi32 -lcrypt32 -luser32 -ladvapi32

TARGET  = pinoader.exe
SOURCES = main.cpp http_client.cpp parser.cpp
OBJECTS = $(SOURCES:.cpp=.o)

.PHONY: all clean strip

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