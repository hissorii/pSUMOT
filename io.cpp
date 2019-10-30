#include <cstdlib> // for malloc(), size_t
#include <cstdio> // for printf()
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
	if (addr != 0x480 && (addr < 0x3000 || addr >= 0x4000)) {
		printf("io r 0x%x\n", addr);
	}
	return *(iop + addr);
}

void IO::write8(u32 addr, u8 data) {
	if (addr < 0x3000 || addr >= 0x4000) {
		printf("io w 0x%x(0x%x)\n", addr, data);
	}
	*(iop + addr) = data;
}

u16 IO::read16(u32 addr) {
	return (*(iop + addr + 1) << 8) + *(iop + addr);
}

void IO::write16(u32 addr, u16 data) {
	*(iop + addr) = data & 0xff;
	*(iop + addr + 1) = data >> 8;
}

u32 IO::read32(u32 addr)
{
	return (*(iop + addr + 3) << 24) + (*(iop + addr + 2) << 16)
		+ (*(iop + addr + 1) << 8) + *(iop + addr);
}
void IO::write32(u32 addr, u32 data)
{
	*(iop + addr) = data & 0xff;
	*(iop + addr + 1) = data >> 8;
	*(iop + addr + 2) = data >> 16;
	*(iop + addr + 3) = data >> 24;
}


} // namespace pSUMOT
