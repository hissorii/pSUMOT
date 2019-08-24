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
#define genregw(x) (reg[x].reg16.lower16)
// ah, ch, dh, bhの順に取り出す
#define genreg8h(x) (reg[x].reg8.h)
// al, cl, dl, blの順に取り出す
#define genreg8l(x) (reg[x].reg8.l)
#if 0
// al, cl, dl, bl, ah, ch, dh, bhの順に取り出す
#define genregb(x) (x < 4 ? reg[x].reg8.l : reg[x - 4].reg8.h)
// 高速化のためu8* genregb[8] = {&al, &cl, &dl, &bl, &ah, &ch, &dh, &bh};にする
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

	char genreg_name[3][NR_GENREG][4] = {
		{"AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"},
		{"AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"},
		{"EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI"}
	};

#define genregb(n) *genregb[n]
	// al, cl, dl, bl, ah, ch, dh, bhの順に取り出す
	u8* genregb[8] = {&al, &cl, &dl, &bl, &ah, &ch, &dh, &bh};

// セグメントレジスタ
#define NR_SEGREG 6
	u16 segreg[NR_SEGREG];
	enum SEGREG {ES, CS, SS, DS, FS, GS};
	char segreg_name[NR_SEGREG][3] = {"ES", "CS", "SS", "DS", "FS", "GS"};

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

/*
  フラグレジスタ
   15                    8|7                     0
  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
  |  |  |  |  |OF|DF|IF|TF|SF|ZF|  |AF|  |PF|  |CF|
  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

  * OFが立つ場合 (最上位ビット(正負を決めるビット)が変わってしまった場合)
     1xxx xxxx           0xxx xxxx
   +)1xxx xxxx         +)0xxx xxxx         加算する数のMSBをそれぞれA, B、
   -----------  または  ----------  つまり 和のMSBをCとすると、A^C&B^C
     0xxx xxxx           1xxx xxxx                             =======
     (正と負の加算はオーバーフローは生じない)

  * AFが立つ場合 (BCD下位の桁(下位4ビット)から桁上りが生じた場合)
     xxx0 xxxx           xxx1 xxxx           xxx0 xxxx           xxx1 xxxx
   +)xxx1 xxxx         +)xxx0 xxxx         +)xxx0 xxxx         +)xxx1 xxxx
   -----------  または -----------  または  ----------  または  ----------
     xxx0 xxxx           xxx0 xxxx           xxx1 xxxx           xxx1 xxxx

   つまり加算する数のMSBをそれぞれA, B、和のMSBをCとすると、A^B^C
                                                            =====
*/
	enum {CF = 1 << 0, PF = 1 << 2, AF = 1 << 4, ZF = 1 << 6, SF = 1 << 7,
	      TF = 1 << 8, IF = 1 << 9, DF = 1 << 10, OF = 1 << 11};
#define OFCLR8 0xf7 // flagu8のOFをクリアするための数
#define OFSET8 0x08 // flagu8のOFをセットするための数
#define IFSET8 0x02 // flagu8のIFをセットするための数
	u8 flag8; // フラグの下位8ビット
	u16 flagu8; // フラグの上位8ビット(16ビットのうち上位8ビットを使う)
	u8 flag_calb[0x200]; // 512バイト
	u8 flag_calw[0x20000]; // 128Kバイト

	BUS *mem, *io;

	u32 get_seg_adr(const SEGREG seg, const u16 a);
	void update_segreg(const u8 seg, const u16 n);

#ifdef CORE_DAS
	void DAS_dump_reg();
	void DAS_prt_post_op(u8 n);
	u8 DAS_nr_disp_modrm(u8 modrm);
	void DAS_modrm16(u8 modrm, bool isReg, bool isDest, bool isWord);
#endif
	u16 modrm16_ea(u8 modrm);
	u16 modrm16w(u8 modrm);
	u8 modrm16b(u8 modrm);
public:
	CPU(BUS* bus);
	void reset();
	void exec();
};

// 参考文献
// (1) https://www.pcjs.org/docs/x86/ops/LOADALL/
// (2) https://www.intel.co.jp/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-system-programming-manual-325384.pdf
