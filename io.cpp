#include <cstdlib> // for malloc(), size_t
#include <cstdio> // for printf()
#include <fstream>
#include "io.h"

/*-----
  xxx *.cppにはusing namespace hoge;ではなくてnamespace hoge {}を使う?
  [2019-07-29]
  -----*/

//using namespace pSUMOT;

namespace pSUMOT {

IO::IO(u32 size) {
	char buf[0x800];
	iop = (u8 *)malloc((size_t)size);
	io = this;

	// SRAM読み込み(UNZ互換)
	std::ifstream fin("cmos.dat", std::ios::in | std::ios::binary);
	if (!fin) {
		printf("can't open cmos.dat");
		goto end;
	}
	fin.read((char *)buf, 0x800);
	fin.close();
	// 偶数アドレスのみに値を再配置していく
	for (int i = 0; i < 0x800; i++) {
		iop[0x3000 + i * 2] = buf[i];
	}
end:
	return;
}
u8 IO::read8(u32 addr) {
	if (addr != 0x480 && (addr < 0x3000 || addr >= 0x4000)) {
		printf("io r 0x%x\n", addr);
	}

	// パッド1
	if (addr == 0x4d0) {
		return 0x7f; // とりあえず入力なしで返す
	}
	// パッド2
	if (addr == 0x4d2) {
		return 0x7f; // とりあえず入力なしで返す
	}
	// RAM size in MB
	if (addr == 0x5e8) {
		return 2; // とりあえず2MBで返す
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
