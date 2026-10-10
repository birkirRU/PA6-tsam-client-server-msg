CXX = g++
CXXFLAGS = -Wall -std=c++17
CXXDEBUGFLAGS = -g

SOURCES_SERVER = common.cpp server.cpp 

SOURCES_CLIENT = common.cpp client.cpp

HEADERS = common.h server.h client.h

all: tsamserver tsamserver_debug tsamclient tsamclient_debug

tsamserver: $(SOURCES_SERVER) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES_SERVER) -o $@

tsamserver_debug: $(SOURCES_SERVER) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CXXDEBUGFLAGS) $(SOURCES_SERVER) -o $@

tsamclient: $(SOURCES_CLIENT) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES_CLIENT) -o $@

tsamclient_debug: $(SOURCES_CLIENT) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CXXDEBUGFLAGS) $(SOURCES_CLIENT) -o $@

clean:
	rm -f tsamserver tsamserver_debug tsamclient tsamclient_debug

.PHONY: all clean