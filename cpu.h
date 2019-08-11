// [2019-06-20] 書き始め
#include "types.h"
#include "bus.h"

/*
  * 80386 レジスタ群

   31               15        7      0
   +----------------+--------+--------+
eax|                |   ah   |   al   |
   +----------------+--------+--------+
                     <----- ax ------->

   +----------------+--------+--------+
ecx|                |   ch   |   cl   |
   +----------------+--------+--------+
                     <----- cx ------->

   +----------------+--------+--------+
edx|                |   dh   |   dl   |
   +----------------+--------+--------+
                     <----- dx ------->

   +----------------+--------+--------+
ebx|                |   bh   |   bl   |
   +----------------+--------+--------+
                     <----- bx ------->

   +----------------+--------+--------+
esp|                |        sp       |
   +----------------+--------+--------+

   +----------------+--------+--------+
ebp|                |        bp       |
   +----------------+--------+--------+

   +----------------+--------+--------+
esi|                |        si       |
   +----------------+--------+--------+

   +----------------+--------+--------+
edi|                |        di       |
   +----------------+--------+--------+
 */

#define NR_GENREG 8 // eax, ebx, ecx, edx, esi, edi, ebp, esp

// mod reg r/m のregにマッチするように並べる
#define eax reg.reg32[0]
#define ecx reg.reg32[1]
#define edx reg.reg32[2]
#define ebx reg.reg32[3]
#define esp reg.reg32[4]
#define ebp reg.reg32[5]
#define esi reg.reg32[6]
#define edi reg.reg32[7]
#define ax reg.reg16[0]
#define cx reg.reg16[1]
#define dx reg.reg16[2]
#define bx reg.reg16[3]
#define sp reg.reg16[4]
#define bp reg.reg16[5]
#define si reg.reg16[6]
#define di reg.reg16[7]
#define ah reg.reg8[0].h
#define al reg.reg8[0].l
#define ch reg.reg8[1].h
#define cl reg.reg8[1].l
#define dh reg.reg8[2].h
#define dl reg.reg8[2].l
#define bh reg.reg8[3].h
#define bl reg.reg8[3].l

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

	char genreg_name16[NR_GENREG][3] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};

// セグメントレジスタ
#define NR_SEGREG 6
	u16 segreg[NR_SEGREG];
	enum SEGREG {CS, DS, ES, SS, FS, GS};

	/*
	 * Segment Descriptor Cache Register
	   - セグメントレジスタ(visible part)に対する「hidden part」あるいは
	     「shadow register」とも呼ばれる
	   - セグメントディスクリプタを並べ替えてCPU内にキャッシュする
	   * 参考文献
	     - (1)のSegment descriptor cache register
	     - (2)のFigure 3-8. Segment Descriptor
	*/
	struct _sdcr {
		u32 limit;
		u32 base; // base address
		// Access Rights (実際は10ビットにpackされているが、使うたびに
		// bit演算したくないので、u8型に分解して使いやすくしておく)
		u8 present;
		u8 dpl;
		u8 system_desc;
		u8 type;
		u8 accessed;
		u8 g; // granularity
		u8 d; // Default operation size
	} sdcr[NR_SEGREG];

	u16 ip;			    // インストラクションポインタ

	BUS *mem, *io;

	void dump_reg();

	u32 get_seg_adr(const SEGREG seg, const u16 a);
	void update_segreg(const SEGREG seg, const u16 n);

public:
	CPU(BUS* bus);
	void reset();
	void exec();
};

// 参考文献
// (1) https://www.pcjs.org/docs/x86/ops/LOADALL/
// (2) https://www.intel.co.jp/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-system-programming-manual-325384.pdf
