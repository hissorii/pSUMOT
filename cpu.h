// [2019-06-20] 書き始め
#include "types.h"
#include "bus.h"

/*
  * 80386 General Registers

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

// mod reg r/m のregの順番にマッチするように並べる
#define eax (reg[0].reg32)
#define ecx (reg[1].reg32)
#define edx (reg[2].reg32)
#define ebx (reg[3].reg32)
#define esp (reg[4].reg32)
#define ebp (reg[5].reg32)
#define esi (reg[6].reg32)
#define edi (reg[7].reg32)
#define ax (reg[0].reg16.lower16)
#define cx (reg[1].reg16.lower16)
#define dx (reg[2].reg16.lower16)
#define bx (reg[3].reg16.lower16)
#define sp (reg[4].reg16.lower16)
#define bp (reg[5].reg16.lower16)
#define si (reg[6].reg16.lower16)
#define di (reg[7].reg16.lower16)
#define ah (reg[0].reg8.h)
#define al (reg[0].reg8.l)
#define ch (reg[1].reg8.h)
#define cl (reg[1].reg8.l)
#define dh (reg[2].reg8.h)
#define dl (reg[2].reg8.l)
#define bh (reg[3].reg8.h)
#define bl (reg[3].reg8.l)

// eax, ecx, edx, ebx, esp ,ebp, esi ,ediの順に取り出す
#define genreg32(x) (reg[x].reg32)
// ax, cx, dx, bx, sp, bp, si, diの順に取り出す
#define genreg16(x) (reg[x].reg16.lower16)
// ah, ch, dh, bhの順に取り出す
#define genreg8h(x) (reg[x].reg8.h)
// al, cl, dl, blの順に取り出す
#define genreg8l(x) (reg[x].reg8.l)
#if 0
// al, cl, dl, bl, ah, ch, dh, bhの順に取り出す
#define genreg8(x) (x < 4 ? reg[x].reg8.l : reg[x - 4].reg8.h)
// 高速化のためu8* genreg8[8] = {&al, &cl, &dl, &bl, &ah, &ch, &dh, &bh};にする
#endif

// とりあえず親クラスはなし
class CPU {
private:
	union g_reg {
		u32 reg32;
	        struct {
#ifdef BIG_ENDIAN
			u16 upper16;
			u16 lower16;
#else
			u16 lower16;
			u16 upper16;
#endif
		} reg16;
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
		} reg8;
	} reg[NR_GENREG];

	char genreg_name16[NR_GENREG][3] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
	char genreg_name32[NR_GENREG][4] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};

	// al, cl, dl, bl, ah, ch, dh, bhの順に取り出す
	u8* genreg8[8] = {&al, &cl, &dl, &bl, &ah, &ch, &dh, &bh};

// セグメントレジスタ
#define NR_SEGREG 6
	u16 segreg[NR_SEGREG];
	enum SEGREG {CS, DS, ES, SS, FS, GS};
	char segreg_name[NR_SEGREG][3] = {"cs", "ds", "es", "ss", "fs", "gs"};

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

	void prt_post_op(u8 n);
	u8 nr_disp_modrm(u8 modrm);
	u16 modrm16w(u8 modrm);
	u8 modrm16b(u8 modrm);
	void disas_modrm16w(u8 modrm);
public:
	CPU(BUS* bus);
	void reset();
	void exec();
};

// 参考文献
// (1) https://www.pcjs.org/docs/x86/ops/LOADALL/
// (2) https://www.intel.co.jp/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-system-programming-manual-325384.pdf
