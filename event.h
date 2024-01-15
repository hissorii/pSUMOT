#include "types.h"

class CPU;

class Event {
private:
	struct _event {
		struct _event *prev;
		struct _event *next;
		s32 now_clks;
		s32 fired_clks;
		s32 delta_clks;
		void (*func)();
	};
	struct _event *head;
	CPU *cpu;
public:
	Event(CPU* cpu);
	void add(s32 clks, void (*func)());
	void check0(void);
	void check(void);
};
