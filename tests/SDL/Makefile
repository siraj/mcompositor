# Bouncing Makefile
# type 'make' to compile
# 'make clean' to remove compilation results (ie. for packaging)

CC = gcc
CFLAGS = -Wall `sdl-config --cflags`
CXXFLAGS = -Wall `sdl-config --cflags`
LIBS  = `sdl-config --libs` -lSDL
SRC = bouncing.c
OBJ = bouncing.o
TARGET = bouncing

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LIBS) -o $(TARGET) $(OBJ)

clean:
	rm -f $(TARGET) $(OBJ) core

# Ricardo Cruz <rick2@aiou.pt>
