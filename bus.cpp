#include <string.h>
#include "bus.h"

/*-----
  [2019-08-08]
  この場合はclass BUS *BUS::～でもBUS *BUS::～でも良かった
  -----*/
BUS *BUS::mem = 0;
BUS *BUS::io = 0;

BUS* BUS::get_bus(const char *s) {
	if (strcmp(s, "mem") == 0) {
		return mem;
	}
	if (strcmp(s, "io") == 0) {
		return io;
	}
	return 0;
}
