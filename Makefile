CC = gcc
CFLAGS = -std=c17 -Wall -Wextra

TARGET = a.out

SRCS = main.c processing.c miniz-3.1.1/miniz.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)