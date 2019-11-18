#include <cstdio> // for printf()
#include <fstream>
#include "dmac.h"

DMAC::DMAC(void) {
	dmac = this;
	working = false;
}

// channelは0～3
void DMAC::dmareq(u8 channel)
{
	u8 mask_val[4] = {1, 2, 4, 8};

	if (mask & mask_val[channel] && devctrl & 4) {
		working = true;
	}
}

u8 DMAC::read8(u32 addr) {
	switch (addr & 0xf) {
	case 0x1:
		return (channel << 2 & 0xf0) | selch[channel & 3];
	case 0x2:
		if (channel & 4) {
			return basecount[channel & 3].count8.lower8;
		} else {
			return curcount[channel & 3].count8.lower8;
		}
	case 0x3:
		if (channel & 4) {
			return basecount[channel & 3].count8.upper8;
		} else {
			return curcount[channel & 3].count8.upper8;
		}
	case 0x4:
		if (channel & 4) {
			return baseaddr[channel & 3].addr8.a0;
		} else {
			return curaddr[channel & 3].addr8.a0;
		}
	case 0x5:
		if (channel & 4) {
			return baseaddr[channel & 3].addr8.a1;
		} else {
			return curaddr[channel & 3].addr8.a1;
		}
	case 0x6:
		if (channel & 4) {
			return baseaddr[channel & 3].addr8.a2;
		} else {
			return curaddr[channel & 3].addr8.a2;
		}
	case 0x7:
		if (channel & 4) {
			return baseaddr[channel & 3].addr8.a3;
		} else {
			return curaddr[channel & 3].addr8.a3;
		}
	case 0x8:
		return (u8)devctrl;
	case 0x9:
		return 0;
	case 0xa:
		return modectrl;
	}


	return 0;
}

void DMAC::write8(u32 addr, u8 data) {
	switch (addr & 0xf) {
	case 0x1:
		channel = data;
		break;
	case 0x2:
		basecount[channel & 3].count8.lower8 = data;
		if (!(channel & 4)) {
			curcount[channel & 3].count8.lower8 = data;
		}
		break;
	case 0x3:
		basecount[channel & 3].count8.upper8 = data;
		if (!(channel & 4)) {
			curcount[channel & 3].count8.lower8 = data;
		}
		break;
	case 0x4:
		baseaddr[channel & 3].addr8.a0 = data;
		if (!(channel & 4)) {
			curaddr[channel & 3].addr8.a0 = data;
		}
		break;
	case 0x5:
		baseaddr[channel & 3].addr8.a1 = data;
		if (!(channel & 4)) {
			curaddr[channel & 3].addr8.a1 = data;
		}
		break;
	case 0x6:
		baseaddr[channel & 3].addr8.a2 = data;
		if (!(channel & 4)) {
			curaddr[channel & 3].addr8.a2 = data;
		}
		break;
	case 0x7:
		baseaddr[channel & 3].addr8.a3 = data;
		// 最上位バイトは共用なので常にカレントにコピーする
		curaddr[channel & 3].addr8.a3 = data;
		break;
	case 0x8:
		devctrl = data; // 上位8bitは常に0
		break;
	case 0x9:
		// 0なので何もしない
		break;
	case 0xa:
		modectrl = data;
		break;
	}

}

u16 DMAC::read16(u32 addr) {
	return (read8(addr + 1) << 8) + read8(addr);
}

void DMAC::write16(u32 addr, u16 data) {
	write8(addr, data & 0xff);
	write8(addr + 1, data >> 8);
}

/*
u32 DMAC::read32(u32 addr) {
	return 0;
}
void DMAC::write32(u32 addr, u32 data) {
}
*/

