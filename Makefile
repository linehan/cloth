CC=gcc
#
#          gprof        
# optimize   |     warnings
# lvl 3 \    |     /    
CFLAGS=-O3 -pg -Wall            
LDFLAGS=-pg 
#        |
#      gprof 
#                                  

SOURCES=cloth.c log.c textutils.c
OBJECTS=$(SOURCES:.c=.o)

EXECUTABLE=cloth

all: $(SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SOURCES) -o $(EXECUTABLE) 

clean:
	rm -f $(OBJECTS) $(EXECUTABLE) gmon.out 
