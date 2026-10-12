CXX = g++
CXXFLAGS = -Wall -std=c++20
CXXDEBUGFLAGS = -g

SOURCES_SERVER = server/server.cpp \
				 server/main.cpp \
				 server/network.cpp 

HEADERS_SERVER = server/poller.h \
				 server/server.h \
				 server/network.h \
			 	 server/logger.h


SOURCES_CLIENT = client.cpp

HEADES_CLIENT = client.h

COMMON_SOURCES = common.cpp

COMMON_HEADERS = common.h

all: tsamserver tsamserver_debug tsamclient tsamclient_debug

tsamserver: $(SOURCES_SERVER) $(COMMON_SOURCES) $(HEADERS_SERVER) $(COMMON_HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES_SERVER) $(COMMON_SOURCES) -o $@

tsamserver_debug: $(SOURCES_SERVER) $(COMMON_SOURCES) $(HEADERS_SERVER) $(COMMON_HEADERS)
	$(CXX) $(CXXFLAGS) $(CXXDEBUGFLAGS) $(SOURCES_SERVER) $(COMMON_SOURCES) -o $@

tsamclient: $(SOURCES_CLIENT) $(COMMON_SOURCES) $(HEADES_CLIENT) $(COMMON_HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES_CLIENT) $(COMMON_SOURCES) -o $@

tsamclient_debug: $(SOURCES_CLIENT) $(COMMON_SOURCES) $(HEADES_CLIENT) $(COMMON_HEADERS)
	$(CXX) $(CXXFLAGS) $(CXXDEBUGFLAGS) $(SOURCES_CLIENT) $(COMMON_SOURCES) -o $@


clean:
	rm -f tsamserver tsamserver_debug tsamclient tsamclient_debug

.PHONY: all clean