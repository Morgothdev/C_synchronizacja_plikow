CC = gcc
CFLAGS = -Wall -g
SRC = server.c
NAME = server
SRC2 = client.c
NAME2 = client
PARAMS = 
LIBS = -lpthread

.PHONY: zad all test clean

all: clean zad

zad:
	$(CC) $(CFLAGS) $(SRC) -o $(NAME) $(LIBS)
	$(CC) $(CFLAGS) $(SRC2) -o $(NAME2) $(LIBS)

clean:
	@rm -f $(NAME) $(NAME2) 

test:
	@echo "Run tests by hand!"
	@echo "Type in first console ./server 4567"
	@echo "Type in second console ./client 4567 127.0.0.1" 
	
