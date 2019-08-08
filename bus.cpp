#include "bus.h"

/*-----
  [2019-08-08]
  この場合はclass BUS *BUS::～でもBUS *BUS::～でも良かった
  -----*/
BUS *BUS::mem = 0;
BUS *BUS::io = 0;

