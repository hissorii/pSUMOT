#include <string.h>
#include "bus.h"

/*-----
  [2019-08-08]
  この場合はclass BUS *BUS::～でもBUS *BUS::～でも良かった
  -----*/
BUS *BUS::mem = 0;
BUS *BUS::io = 0;
BUS *BUS::dmac = 0;
BUS *BUS::cdc = 0;
Event *BUS::ev = 0;

BUS* BUS::get_bus(const char *s) {
	if (strcmp(s, "mem") == 0) {
		return mem;
	}
	if (strcmp(s, "io") == 0) {
		return io;
	}
	if (strcmp(s, "dmac") == 0) {
		return dmac;
	}
	return 0;
}

void BUS::set_ev(Event *ev) {
	this->ev = ev;
}
