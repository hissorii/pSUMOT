TARGET = psumot

# for debugging
CXXFLAGS += -Wall -g

# for core debugging
CXXFLAGS += -DCORE_DAS

CXXFLAGS += `sdl2-config --cflags`

OBJS = main.o cpu.o memory.o io.o bus.o dmac.o cdc.o event.o
LIBS = `sdl2-config --libs`

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LIBS)

#dependencies
cpu.o: cpu_clocks.h cpu_macros.h cpu.h bus.h types.h
main.o: io.h cpu.h memory.h types.h
memory.o: memory.h bus.h types.h
io.o: io.h bus.h types.h
bus.o: bus.h types.h
dmac.o: dmac.h bus.h types.h
cdc.o: event.h cdc.h bus.h types.h
event.o: event.h cpu.h

clean:
	rm -f Makefile~ *.cpp~ *.h~ $(OBJS) $(TARGET) 
