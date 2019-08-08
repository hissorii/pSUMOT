TARGET = psumot

# for debugging
CXXFLAGS += -Wall

OBJS = main.o cpu.o memory.o io.o

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS)

#dependencies
cpu.o: cpu.h memory.h types.h
main.o: io.h cpu.h memory.h types.h
memory.o: io.h memory.h types.h
io.o: io.h types.h

clean:
	rm -f Makefile~ *.cpp~ *.h~ $(OBJS) $(TARGET) 
