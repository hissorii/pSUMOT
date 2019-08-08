TARGET = psumot

# for debugging
CXXFLAGS += -Wall

OBJS = main.o cpu.o memory.o io.o bus.o

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS)

#dependencies
cpu.o: cpu.h memory.h types.h
main.o: io.h cpu.h memory.h types.h
memory.o: memory.h bus.h types.h
io.o: io.h bus.h types.h
bus.o: bus.h types.h

clean:
	rm -f Makefile~ *.cpp~ *.h~ $(OBJS) $(TARGET) 
