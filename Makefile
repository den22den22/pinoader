# Compiler settings
CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -D_WIN32_WINNT=0x0601 -m32 -Wno-unknown-pragmas \
           -static-libgcc -static-libstdc++

# OpenSSL Include path (from your structure)
OPENSSL_INC = -I"C:/Users/PC/openssl/include"

# Direct DLL Linking (The one that worked!)
# Point to the DLLs in your openssl/bin folder if they exist there, 
# OR copy libssl-3.dll and libcrypto-3.dll to your project folder and link like this:
LIB_SSL     = libssl-3.dll
LIB_CRYPTO  = libcrypto-3.dll

TARGET  = pinoader.exe
SOURCES = main.cpp http_client.cpp parser.cpp
OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
    # Linking against local DLLs
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) $(LIB_SSL) $(LIB_CRYPTO) -lws2_32 -lgdi32 -lcrypt32

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(OPENSSL_INC) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)