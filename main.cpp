#include <iostream>
#include "memory.h"
#include "io.h"
#include "cpu.h"

int main(void)
{
	// RAM 6MB
	Memory mem((u32)0x600000);
	pSUMOT::IO io(0x10000);

	CPU cpu(&mem);

#if 0
	int i;
	for (i = 0xf8000; i < 0xf8050; i++) {
		std::cout << std::hex << (int)mem.read8(i);
	}
#endif

	cpu.reset();
	for (int i = 0; i < 128; i++)
		cpu.exec();
}
