TARGET = psumot

# for debugging
CXXFLAGS += -Wall -g

# for core debugging
CXXFLAGS += -DCORE_DAS

# for video test
#CXXFLAGS += -DVIDEO_TEST

CXXFLAGS += `sdl2-config --cflags`

OBJS = main.o cpu.o memory.o io.o bus.o
LIBS = `sdl2-config --libs`

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LIBS)

#dependencies
cpu.o: cpu.h bus.h types.h
main.o: io.h cpu.h memory.h types.h
memory.o: memory.h bus.h types.h
io.o: io.h bus.h types.h
bus.o: bus.h types.h

clean:
	rm -f Makefile~ *.cpp~ *.h~ $(OBJS) $(TARGET) 
