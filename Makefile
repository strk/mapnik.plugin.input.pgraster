# To use clang, run:  make CXX=clang++

CXXFLAGS = $(shell mapnik-config --cflags) -fPIC

LIBS = $(shell mapnik-config --libs --ldflags --dep-libs)

SRC = $(wildcard *.cpp)

OBJ = $(SRC:.cpp=.o)

BIN = pgraster.input

all : $(SRC) $(BIN)

$(BIN) : $(OBJ)
	$(CXX) -shared $(OBJ) $(LIBS) -o $@

.cpp.o :
	$(CXX) -c $(CXXFLAGS) $< -o $@

.PHONY : clean

clean:
	rm -f $(OBJ)
	rm -f $(BIN)

deploy : all
	cp pgraster.input $(shell mapnik-config --input-plugins)

install: all deploy
