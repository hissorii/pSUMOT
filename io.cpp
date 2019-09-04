#include <cstdlib> // for malloc(), size_t
#include "io.h"

/*-----
  xxx *.cppにはusing namespace hoge;ではなくてnamespace hoge {}を使う?
  [2019-07-29]
  -----*/

//using namespace pSUMOT;

namespace pSUMOT {

IO::IO(u32 size) {
	iop = (u8 *)malloc((size_t)size);
	io = this;
}
u8 IO::read8(u32 addr) {
	return *(iop + addr);
}

void IO::write8(u32 addr, u8 data) {
	*(iop + addr) = data;
}

u16 IO::read16(u32 addr) {
	return (*(iop + addr + 1) << 8) + *(iop + addr);
}

void IO::write16(u32 addr, u16 data) {
	*(iop + addr) = data & 0xff;
	*(iop + addr + 1) = data >> 8;
}


} // namespace pSUMOT
