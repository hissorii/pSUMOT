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
#define genregd(x) (reg[x].reg32)
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
	enum REGSIZE {byte, word, dword, fword};

#define genregb(n) *genregb[n]
	// al, cl, dl, bl, ah, ch, dh, bhの順に取り出す
	u8* genregb[8] = {&al, &cl, &dl, &bl, &ah, &ch, &dh, &bh};

/*
  Global Descriptor Table Register
  47               31                               0
  +----------------+--------------------------------+
  |     Limit      |           Base Address         |
  +----------------+--------------------------------+
           |                        |
+-----------------------------------+
|          |
|          +------------------------------------------------------------------+
|  Descriptor Table                                                           |
|     0       1      2       3       4        5           6             7     V
+->+------+-------+------+-------+--------+-------+-----------------+--------+-
   |Lim7:0|Lim15:8|Bas7:0|Bas15:8|Bas23:16|Attr7:0|Attr12:8|Lim19:16|Bas31:24|A
   +------+-------+------+-------+--------+-------+-----------------+--------+|
   |Lim7:0|Lim15:8|Bas7:0|Bas15:8|Bas23:16|Attr7:0|Attr12:8|Lim19:16|Bas31:24||
   +------+-------+------+-------+--------+-------+-----------------+--------+|
                                      :                                       |
   +------+-------+------+-------+--------+-------+-----------------+--------+|
   |Lim7:0|Lim15:8|Bas7:0|Bas15:8|Bas23:16|Attr7:0|Attr12:8|Lim19:16|Bas31:24|V
   +------+-------+------+-------+--------+-------+-----------------+--------+-
    Lim:Limit, Bas:Base Address, Attr:Attribute

   Attribute
   11 10  9  8  7 65  4  321 0
   +-----------+--------------+
   |G|D/B|0|AVL|P|DPL|S|Type|A|
   +-----------+--------------+
    G: 0:SegSizeは1B単位(1B～1MB) / 1:SegSizeは4KB単位(4KB～4GB)
    D/B: 1:32bit / 0:16bit
    AVL: OSが自由に使用可能
    P: 1:セグメントがメモリに存在する / 0:セグメントがメモリに存在しない
    DPL: 特権レベル
    S: 0:システムセグメント / 1:コードまたはデータセグメント
    A: セグメントがアクセスされた
    Type: 0:読み出し専用データセグメント / 1:読み書き可能データセグメント
          2:読み出し専用スタックセグメント / 3:読み書き可能スタックセグメント
          4:実行専用コードセグメント / 5: 実行及び読み出し可能コードセグメント
          6: 実行専用コンフォーミングコードセグメント
          7:実行及び読み出し可能コンフォーミングコードセグメント

    参考文献 (3), (4)p.193

 */

	struct _gdtr {
		u16 limit;
		u32 base;
	} gdtr;



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
		u16 attr;
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
   -----------  または  ----------  つまり 和のMSBをCとすると、(A^C&B^C)==1
     0xxx xxxx           1xxx xxxx                              =======
     (正と負の加算はオーバーフローは生じない)

  * OFが立つ場合(減算) (最上位ビット(正負を決めるビット)が変わってしまった場合)
    負-正で正になって   正-負で負になって
    しまった場合        しまった場合
     1xxx xxxx           0xxx xxxx
   -)0xxx xxxx         -)1xxx xxxx         減算する数(A-B)のMSBをそれぞれA, B、
   -----------  または  ----------  つまり 和のMSBをCとすると、(A^C&A^B)==1
     0xxx xxxx           1xxx xxxx                              =======
     (正と正の減算、負と負の減算はオーバーフローは生じない)

  * AFが立つ場合 (BCD下位の桁(下位4ビット)から桁上りが生じた場合)
     xxx0 xxxx           xxx1 xxxx           xxx0 xxxx           xxx1 xxxx
   +)xxx1 xxxx         +)xxx0 xxxx         +)xxx0 xxxx         +)xxx1 xxxx
   -----------  または -----------  または  ----------  または  ----------
     xxx0 xxxx           xxx0 xxxx           xxx1 xxxx           xxx1 xxxx

   つまり加算する数のMSBをそれぞれA, B、和のMSBをCとすると、A^B^C
                                                            =====
*/
	enum {CF = 1 << 0, PF = 1 << 2, AF = 1 << 4, ZF = 1 << 6, SF = 1 << 7,
	      TF = 1 << 8, IF = 1 << 9, DF = 1 << 10, OF = 1 << 11,
	      DF8 = 1 << 2};
#define TFSET8 0x01 // flagu8のTFをセットするための数
#define IFSET8 0x02 // flagu8のIFをセットするための数
#define DFSET8 0x04 // flagu8のDFをセットするための数
#define OFSET8 0x08 // flagu8のOFをセットするための数
	u8 flag8; // フラグの下位8ビット
	u16 flagu8; // フラグの上位8ビット(16ビットのうち上位8ビットを使う)
	u8 flag_calb[0x200]; // 512バイト
	u8 flag_calw[0x20000]; // 128Kバイト

	u8 modrm_add_seg[2][3][8] = {
		{{DS, DS, SS, SS, DS, DS, DS, DS},
		 {DS, DS, SS, SS, DS, DS, SS, DS},
		 {DS, DS, SS, SS, DS, DS, SS, DS}},
		{{DS, DS, DS, DS, DS, DS, DS, DS}, /* 32bit分は要確認 */
		 {DS, DS, DS, DS, DS, SS, DS, DS},
		 {DS, DS, DS, DS, DS, SS, DS, DS}}
	};

/*
  Control Register 0


 */
	u32 cr[4];


	u8 sar_bitb[8] = {0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe};
	u16 sar_bitw[16] = {0x0000, 0x8000, 0xc000, 0xe000, 0xf000, 0xf800, 0xfc00, 0xfe00, 0xff00, 0xff80, 0xffc0, 0xffe0, 0xfff0, 0xfff8, 0xfffc, 0xfffe};

	u8 seg_ovride = 0;
	bool opsize_ovride = false;
	bool addrsize_ovride = false;
	bool repne_prefix = false;
	bool repe_prefix = false;
	enum SIZEPRFX {size16, size32};
	SIZEPRFX opsize, addrsize;
	bool isRealMode;

	BUS *mem, *io;

	u32 get_seg_adr(const SEGREG seg, const u16 a);
	void update_segreg(const u8 seg, const u16 n);

#ifdef CORE_DAS
	bool DAS_hlt;

	void DAS_dump_reg();
	void DAS_prt_post_op(u8 n);
	void DAS_modrm(u8 modrm, bool isReg, bool isDest, REGSIZE regsize);
	void DAS_prt_rmr_rrm(const char *s, bool isReg, bool isDest, REGSIZE regsize);
#endif
	u8 nr_disp_modrm(u8 modrm);
	u16 modrm16_ea(u8 modrm);
	u32 modrm32_ea(u8 modrm);
	u32 modrm16_seg_ea(u8 modrm);
	u32 modrm_seg_ea(u8 modrm);
	u32 modrm16d(u8 modrm);
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
// (3)  http://caspar.hazymoon.jp/OpenBSD/annex/intel_segment.html
// (4) はじめて読む486 株式会社アスキー
// (5) 80x86/80x86 ファミリー・テクニカルハンドブック
