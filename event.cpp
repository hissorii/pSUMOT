#include <cstdlib> // for malloc()
#include <cstdio> // for printf()
#include "event.h"
#include "cpu.h"

Event::Event(CPU *cpu) {
	this->cpu = cpu;
}
void Event::add(s32 clks, void (*func)()) {
	struct _event *event, *prev, *next, *last, *tmp;
	event = (struct _event *)malloc(sizeof(struct _event));
	if (head != NULL) {
		for (tmp = head; tmp != NULL; last = tmp, tmp = tmp->next) {
			if (clks < tmp->fired_clks) {
				event->prev = tmp->prev;
				tmp->prev = event;
				event->next = tmp;
				break;
			}
		}
		if (tmp == NULL) {
			event->prev = last;
			last->next = event;
			event->next = NULL;
		}
	} else {
		head = event;
	}
	event->now_clks = cpu->clks;
	event->fired_clks = cpu->clks - clks;
	cpu->exit_clks = event->fired_clks < 0? 0 : cpu->clks - clks;
	prev = event->prev;
	if (prev) {
		event->delta_clks = prev->fired_clks - event->fired_clks;
	}
	next = event->next;
	if (next) {
		next->delta_clks = event->fired_clks - next->fired_clks;
	}
}

void Event::check0(void) {
	if (head == NULL) {
		return;
	}
	if (head->fired_clks < 0) {
		head->fired_clks += cpu->remains_clks;
		if (head->fired_clks < 0) {
			return;
		}
		cpu->exit_clks = head->fired_clks;
		return;
	}
	printf("xxx check0()\n");
}

void Event::check(void) {
	struct _event *event;
	if (head == NULL) {
		return;
	}
	for (event = head; event != NULL; event = event->next) {
		if (event->fired_clks > 0 && cpu->clks <= event->fired_clks) {
			(*event->func)();
			// リストから外す		
			if (event->prev) {
				event->prev->next = event->next;
			} else {
				head = event->next;
			}
			if (event->next) {
				event->next->prev = event->prev;
			}
			free(event);
		} else {
			break;
		}
	}
}
