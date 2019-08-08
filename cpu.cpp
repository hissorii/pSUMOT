#include "memory.h"
#include "cpu.h"

// 引数aにCSを加算したアドレスを返却する
u32 CPU::get_cs_adr(u16 a) {
	// 8086の場合は20ビットでマスクする
	return ((u32)cs << 4) + a;
}

void CPU::reset() {
	cs = 0xf000;
	ip = 0xfff0;
}
void CPU::exec() {
	u8 op;
	op = mem->read8(get_cs_adr(ip++));

	switch (op) {
	case 0x00:
		break;
	}
}
