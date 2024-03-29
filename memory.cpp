#include <cstdlib> // for malloc(), size_t, exit()
#include <cstring> // for memset()
#include <fstream>
#include "memory.h"

Memory::Memory(u32 size) {
	ram = (u8 *)malloc((size_t)size);
	memset(ram, 0, size);
	ram_size = size;
	sysrom = (u8 *)malloc((size_t)SYSROM_SIZE);
	osrom = (u8 *)malloc((size_t)OSROM_SIZE);
	vram = (u8 *)malloc((size_t)VRAM_SIZE);
	memset(vram, 0, VRAM_SIZE);
	mem = this;
	
	// システムROMの読み込み
	std::ifstream fin("roms/FMT_SYS.ROM", std::ios::in | std::ios::binary);
	if (!fin) {
		exit(1);
	}
	fin.read((char *)sysrom, SYSROM_SIZE);
	fin.close();

	// OS-ROMの読み込み
	std::ifstream fin2("roms/FMT_DOS.ROM", std::ios::in | std::ios::binary);
	if (!fin2) {
		exit(1);
	}
	fin2.read((char *)osrom, OSROM_SIZE);
	fin2.close();

}

/* memory mapped I/O */
// グラフィックVRAM更新モードレジスタ
#define GVRAM_UPD_REG 0xcff81
// グラフィックVRAMページセレクトレジスタ
#define GVRAM_PGSEL_REG 0xcff83

u8 Memory::read8(u32 addr) {
	u8 tmp, tmp2;

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

	// BOOT ROM
	if (addr >= 0xf8000 && addr <= 0xfffff) {
		if (!(io->read8(0x480) & 2)) {
			// ブートROM領域内の値を返す
			return *(sysrom + 0x38000 + addr - 0xf8000);
		}
		return *(ram + addr);		
	}


	/*
	  下記アドレスのVRAMはバンク切り替えで8枚ある。
	  xxx それぞれ0x80000000, 0x80008000, 0x80010000,...にマッピングされる?

	  0xc0000+------------+
		 |  vram 32KB |
	  0xc8000+------------+
		 |I/OCVRAM32KB|
	  0xd0000+------------+
		 |辞書ROM 32KB|
	  0xd8000+------------+
		 |            |
		 |            |
	  0xeffff+------------+

	 */
	// メインメモリ/VRAM (I/O 0x404の7bit目で決まる)
	if (addr >= 0xc0000 && addr < 0xf0000) {
		if (io->read8(0x404) & 0x80) {
			return *(ram + addr);
		}
		if (addr >= 0xc0000 && addr < 0xc8000) {
			tmp = this->read8(GVRAM_UPD_REG);
			tmp2 = this->read8(GVRAM_PGSEL_REG);
			return *(vram + (tmp >> 6) * 0x8000 + ((tmp2 >> 4) & 1) * 0x20000 + addr - 0xc0000);
		}
	}

	// RAM
	if (addr < ram_size) {
		return *(ram + addr);
	}

	// VRAM
	if (addr >= 0x80000000 && addr < 0x80080000) {
		return *(vram + (addr - 0x80000000));
	}
	if (addr >= 0x80100000 && addr < 0x80180000) {
		return *(vram + (addr - 0x80100000));
	}

	// SYSTEM ROM(BOOT ROM)
	if (addr >= 0xfffc0000) {
		// システムROM領域内の値を返す
		return *(sysrom + (addr - 0xfffc0000));
	}

	if (addr >= 0xc2000000 && addr < 0xc2080000) {
		return *(osrom + (addr - 0xc2000000));
	}

	printf("not coded yet. read addr=0x%x\n\n", addr);
	exit(1);
}

void Memory::write8(u32 addr, u8 data) {
	u8 tmp, tmp2;
	u32 addr2;
	u8 *p;

	// メインメモリ/VRAM (I/O 0x404の7bit目で決まる)
	if (addr >= 0xc0000 && addr < 0xf0000) {
		if (io->read8(0x404) & 0x80) {
			*(ram + addr) = data;
			return;
		}
		if (addr >= 0xc0000 && addr < 0xc8000) {
			tmp = this->read8(GVRAM_UPD_REG);
			tmp2 = this->read8(GVRAM_PGSEL_REG);
			p = vram + ((tmp2 >> 4) & 1) * 0x20000;
			addr2 = addr - 0xc0000;
			// 各プレーンに書き込み
			if (tmp & 1) {
				*(p + addr2) = data;
			}
			if (tmp & 2) {
				*(p + 0x8000 + addr2) = data;
			}
			if (tmp & 4) {
				*(p + 0x10000 + addr2) = data;
			}
			if (tmp & 8) {
				*(p + 0x18000 + addr2) = data;
			}
			return;
		}
	}

	// RAM
	if (addr < ram_size) {
		*(ram + addr) = data;
		return;
	}

	// VRAM
	if (addr >= 0x80000000 && addr < 0x80080000) {
	  //		printf("w vram addr=0x%x(0x%x)\n", addr, data);
		*(vram + (addr - 0x80000000)) = data;
		return;
	}
	if (addr >= 0x80100000 && addr < 0x80180000) {
	  //		printf("w vram addr=0x%x(0x%x)\n", addr, data);
		*(vram + (addr - 0x80100000)) = data;
		return;
	}

	printf("not cocded yet. write addr=0x%x\n\n", addr);
	exit(1);
}

u16 Memory::read16(u32 addr) {
	return (read8(addr + 1) << 8) + read8(addr);
}

void Memory::write16(u32 addr, u16 data) {
	write8(addr, (u8)data);
	write8(addr + 1, data >> 8);
}

u32 Memory::read32(u32 addr) {
	return (read8(addr + 3) << 24) + (read8(addr + 2) << 16) + (read8(addr + 1) << 8) + read8(addr);
}

void Memory::write32(u32 addr, u32 data) {
	write8(addr, (u8)data);
	write8(addr + 1, data >> 8);
	write8(addr + 2, data >> 16);
	write8(addr + 3, data >> 24);
}
