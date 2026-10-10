CXX = g++
CXXFLAGS = -Wall -std=c++17
CXXDEBUGFLAGS = -g

SOURCES_SERVER = common.cpp server.cpp 

SOURCES_CLIENT = common.cpp client.cpp

HEADERS = common.h server.h client.h

all: tsamserver tsamserver_debug tsamclient tsamclient_debug

tsamserver: $(SOURCES_SERVER) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@

tsamserver_debug: $(SOURCES_SERVER) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CXXDEBUGFLAGS) $(SOURCES) -o $@

tsamclient: $(SOURCES_CLIENT) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@

tsamclient_debug: $(SOURCES_CLIENT) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CXXDEBUGFLAGS) $(SOURCES) -o $@

clean:
	rm -f server server_debug client client_debug

.PHONY: all clean