#include <iostream>
#include "memory.h"
#include "io.h"
#include "cpu.h"

int main(void)
{
	// RAM 6MB
	Memory mem((u32)0x600000);

	CPU cpu(&mem);
	pSUMOT::IO io(0x10000);
	
	int i;
	for (i = 0xf8000; i < 0xf8050; i++) {
		std::cout << std::hex << (int)mem.read8(i);
	}
	return(0);

	cpu.reset();
	while (1) {
		cpu.exec();
	}
}
