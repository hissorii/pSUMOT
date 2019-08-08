// [2019-06-20] 書き始め
#include "types.h"
/*
  * 80386 レジスタ群

   31               15        7      0
   +----------------+--------+--------+
eax|                |   ah   |   al   |
   +----------------+--------+--------+
                     <----- ax ------->

   +----------------+--------+--------+
ebx|                |   bh   |   bl   |
   +----------------+--------+--------+
                     <----- bx ------->

   +----------------+--------+--------+
ecx|                |   ch   |   cl   |
   +----------------+--------+--------+
                     <----- cx ------->

   +----------------+--------+--------+
edx|                |   dh   |   dl   |
   +----------------+--------+--------+
                     <----- dx ------->

   +----------------+--------+--------+
esi|                |        si       |
   +----------------+--------+--------+

   +----------------+--------+--------+
edi|                |        di       |
   +----------------+--------+--------+

   +----------------+--------+--------+
ebp|                |        bp       |
   +----------------+--------+--------+

   +----------------+--------+--------+
esp|                |        sp       |
   +----------------+--------+--------+
 */

#define NR_GENREG 8 // eax, ebx, ecx, edx, esi, edi, ebp, esp

#define eax reg.reg32[0]
#define ebx reg.reg32[1]
#define ecx reg.reg32[2]
#define edx reg.reg32[3]
#define esi reg.reg32[4]
#define edi reg.reg32[5]
#define ebp reg.reg32[6]
#define esp reg.reg32[7]
#define ax reg.reg16[0]
#define bx reg.reg16[1]
#define cx reg.reg16[2]
#define dx reg.reg16[3]
#define si reg.reg16[4]
#define di reg.reg16[5]
#define bp reg.reg16[6]
#define sp reg.reg16[7]
#define ah reg.reg8[0].h
#define al reg.reg8[0].l
#define bh reg.reg8[1].h
#define bl reg.reg8[1].l
#define ch reg.reg8[2].h
#define cl reg.reg8[2].l
#define dh reg.reg8[3].h
#define dl reg.reg8[3].l

class Memory;

// とりあえず親クラスはなし
class CPU {
private:
	union g_reg {
		u32 reg32[NR_GENREG];
	        struct {
			u16 upper16;
			u16 lower16;
		} reg16[NR_GENREG];
		struct {
#ifdef BIG_ENDIAN
			u16 upper16;
			u8 h;
			u8 l;
#else
			u8 l;
			u8 h;
			u16 upper16;
#endif			
		} reg8[NR_GENREG];
	} reg;
	u16 cs, ip;
	
	Memory *mem;

	u32 get_cs_adr(u16 a);

public:
	CPU(Memory *m) {
		mem = m;
	}
	void reset();
	void exec();
};
