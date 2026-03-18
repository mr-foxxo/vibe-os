# Makefile for the compatibility layer (libcompat)

# Compiler and flags
CC = i686-elf-gcc
AR = i686-elf-ar
CFLAGS = -O2 -g -Wall -Wextra -Werror -I./include -I../../../headers/

# Source directories
VPATH = string stdio stdlib

# Object files
OBJS = string.o stdio.o stdlib.o

# Library to build
LIB = libcompat.a

# Default target
all: $(LIB)

# Rule to build the library
$(LIB): $(OBJS)
	$(AR) rcs $(LIB) $(OBJS)

# Rule to build object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean rule
clean:
	rm -f $(OBJS) $(LIB)

.PHONY: all clean
