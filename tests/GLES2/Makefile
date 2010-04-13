CC = gcc
CFLAGS = -Wall `pkg-config --cflags x11`
LIBS = `pkg-config --libs x11` -lGLESv2 -lEGL
SRC = test-gles2.c
OBJ = test-gles2.o
TARGET = test-gles2

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LIBS) -o $(TARGET) $(OBJ)

clean:
	rm -f $(TARGET) $(OBJ) core *~
