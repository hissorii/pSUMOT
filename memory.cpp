#include <cstdlib> // for malloc(), size_t, exit()
#include <fstream>
#include "memory.h"
#include "io.h"

Memory::Memory(u32 size) {
	ram = (u8 *)malloc((size_t)size);
	sysrom = (u8 *)malloc((size_t)SYSROM_SIZE);
	
	// システムROMの読み込み
	std::ifstream fin("roms/FMT_SYS.ROM", std::ios::in | std::ios::binary);
	if (!fin) {
		exit(1);
	}
	fin.read((char *)sysrom, SYSROM_SIZE);
	fin.close();
}

u8 Memory::read8(u32 addr) {
	/*
	  *1 ブートROM(システムROMの後半32KB)は、リセット時と
             I/O 048Hの操作時のみ、F8000H～FFFFFHにマッピングされる
	     (FM TOWNS テクニカルデータブック p10)

	  00000000H+-------------+
	           |             |
	           =             =
	           |             |
	  000F8000H+-------------+ *1
		   |ブートROM/RAM|<---------------+
	  000FFFFFH+-------------+                |
	           |             |                |
		   =             =                |
		   |             |                |
	  FFFC0000H+-------------+-               |
		   |システムROM  |A               |
	  FFFF8000H+- - - - - - -+| FMT_SYS.ROM   |
		   |(ブートROM)  |V---------------+
	  FFFFFFFFH+-------------+-	   

	 */
	if (addr >= 0xf8000 && addr <= 0xfffff) {
		if (!(io->read8(0x480) & 2)) {
			// ブートROM領域内の値を返す
			return *(sysrom + 0x38000 + addr - 0xf8000);
		}

	}
	return *(ram + addr);
}

void Memory::write8(u32 addr, u8 data) {
	*(ram + addr) = data;
}

void Memory::setio(pSUMOT::IO *i) {
	io = i;
}
