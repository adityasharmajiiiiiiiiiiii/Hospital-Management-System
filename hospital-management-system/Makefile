CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread

hms: backend/main.cpp
	$(CXX) $(CXXFLAGS) -o hms backend/main.cpp

run: hms
	./hms 8080 frontend data

clean:
	rm -f hms

.PHONY: run clean
