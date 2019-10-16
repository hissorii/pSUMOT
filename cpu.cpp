#include <cstdio> // for printf()
#include "cpu.h"

using namespace std; // for printf()

CPU::CPU(BUS* bus) {
	mem = bus->get_bus("mem");
	io = bus->get_bus("io");

	// バイト同士の演算によるフラグSF/ZF/PF/CFの状態をあらかじめ算出する
	// キャリーフラグ算出のため、配列長は9ビットである
	for (int i = 0; i < 0x200; i++) {
		u8 sf, zf, pf, cf;
		sf = (i & 0x80)? SF : 0;
		zf = (i == 0)? ZF : 0;
		pf = i;
		pf ^= pf >> 4;
		pf ^= pf >> 2;
		pf ^= pf >> 1;
		pf = (pf & 1)? 0 : PF;
		cf = (i & 0x100)? CF : 0;
		flag_calb[i] = sf | zf | pf | cf;
	}
	// ワード同士の演算によるフラグSF/ZF/PF/CFの状態をあらかじめ算出する
	// キャリーフラグ算出のため、配列長は17ビットである
	for (int i = 0; i < 0x20000; i++) {
		u8 sf, zf, pf, cf;
		sf = (i & 0x8000)? SF : 0;
		zf = (i == 0)? ZF : 0;
		pf = (u8)(i & 0xff); // パリティは下位8ビットのみチェックする
		pf ^= pf >> 4;
		pf ^= pf >> 2;
		pf ^= pf >> 1;
		pf = (pf & 1)? 0 : PF;
		cf = (i & 0x10000)? CF : 0;
		flag_calw[i] = sf | zf | pf | cf;
	}
	// ダブルワード同士の演算をあらかじめ算出しておくには、配列長33ビットの
	// 配列を用意しなければならないため現実的ではない。
	// そのため、ダブルワード同士の演算によるフラグはその都度算出する。
}

void CPU::reset() {
	opsize = size16;
	addrsize = size16;
	isRealMode = true;

	for (int i = 0; i < NR_SEGREG; i++) segreg[i] = 0x0000;
	for (int i = 0; i < 4; i++) cr[i] = 0;
	segreg[CS] = 0xf000;
	eip = 0xfff0;
	edx = 0x672; // xxxなんか入れないとだめみたい
	/*
	  386リセット時は、コードセグメントのセグメントディスクリプタ
	  キャッシュのセグメントベースが、0xffff0000になっている?
	  (80286の場合の話が以下のディスクリプターキャッシュの項に記載あり
	  https://ja.wikipedia.org/wiki/Intel_80286 )
	  のでリアルモードでも0xfffffff0から命令フェッチを開始する?
	  そしてセグメントを超えたfar jumpをした時にcsがリロードされて
	  セグメントディスクリプタキャッシュのセグメントベースはcs<<4に戻る?
	 */
	sdcr[CS].base = 0xffff0000;
	flag8 = 0;
	cr[0] = 0x60000010;

#ifdef CORE_DAS
	DAS_hlt = false;
#endif
}

#ifdef CORE_DAS // CORE_DAS stands for cpu CORE DisASsembler
/*
  以下の様なレジスタの状態を出力する
eax:b6f90000 ecx:bea69328 edx:123431cc ebx:85f4d995   eflags:00000000
esp:000002de ebp:b6f7b498 esi:b6f9eb58 edi:bea69340
cs:fc00 ds:0000 es:0000 ss:0000 fs:0000 gs:0000
*/
void CPU::DAS_dump_reg() {
	int i;
	static int step = 1;

	for (i = 0; i < 4; i++) {
		printf("%s:%08x ", genreg_name[2][i], genregd(i));
	}
	printf("  eflags:%08x", (u16)flagu8 << 8 | flag8);
	printf("  %4d", step++);
	printf("\n");
	for (i = 4; i < NR_GENREG; i++) {
		printf("%s:%08x ", genreg_name[2][i], genregd(i));
	}
	printf("     eip:%08x", eip);
	printf("\n");
	for (i = 0; i < NR_SEGREG; i++) {
		printf("%s:%04x ", segreg_name[i], segreg[i]);
	}
	printf("         cr0:%08x", cr[0]);
	printf("\n\n");

#if 1
	int j, startadr;
	if (step == 120) {
                for (i = 0; i < 32; i++) {
                        printf("0x%02x ", mem->read8(0xf7fb0 + i));
                        if (((i + 1) % 16) == 0) printf("\n");
                }
                printf("\n");
        }
	if (step == 50140) {
		startadr = 0xf7f00;
		for (i = 0; i < 16; i++) {
			printf("%04x ", i * 16 + startadr);
			for (j = 0; j < 16; j++) {
				printf("%02x ", mem->read8(startadr + i * 16 + j));
			}
			printf("\n");
                }
	}
#endif
}

void CPU::DAS_prt_post_op(u8 n) {
	int i;
	for (i = 0; i < n; i++)
		printf(" %02x", mem->read8(get_seg_adr(CS, eip + i)));
	for (i = 0; i < 5 - n; i++)
		printf("%3c", ' ');
}

// isReg: mod reg R/M の reg が存在するか
// isDest: mod reg R/M の reg がDestinationになるか
// regsize: バイト/ワード/ダブルワード/Fワード転送
//   ただし、ワード指定でもopsizeがsize32の場合は関数内でダブルワードに変換する
// POP m16でregのないModR/Mでコンマ不要の場合はisReg=false, isDest=trueにする
void CPU::DAS_modrm(u8 modrm, bool isReg, bool isDest, REGSIZE regsize) {
	u8 mod, reg, rm, sib, idx, base;
	u32 tip; // tmp ip
#define NR_RM 8
	char addressing_str[2][NR_RM][13] = {
	  {"[BX + SI", "[BX + DI", "[BP + SI", "[BP + DI", "[SI", "[DI", "[BP", "[BX", },
	  {"[EAX", "[ECX", "[EDX", "[EBX", "[EAX + EBX*8", "[EBP", "[ESI", "[EDI", }
	};
	char disp[] = " + 0x????????";
	char sib_scale_str[4][3] = {"", "*2", "*4", "*8"};

	if (regsize == word && opsize == size32) {
		regsize = dword;
	}
	char sizestr[4][6] = {"byte", "word", "dword", "fword"};

	if (isReg && isDest) {
		reg = modrm >> 3 & 7;
		printf("%s, ", genreg_name[regsize][reg]);
	}
	mod = modrm >> 6;
	rm = modrm & 7;

	if (mod == 3) {
		printf("%s%s", genreg_name[regsize][rm], isDest?"\n\n":", ");
		if (isReg && !isDest) {
			reg = modrm >> 3 & 7;
			printf("%s\n\n", genreg_name[regsize][reg]);
		}
		return;
	}

	printf("%s ptr ", sizestr[regsize]);

	// [disp16]
	if (addrsize == size16 && rm == 6 && mod == 0) {
		printf("[0x%04x]%s", mem->read16(get_seg_adr(CS, eip + 1)), isDest?"\n\n":", ");
		return;
	}

	// [disp32]
	if (addrsize == size32 && rm == 5 && mod == 0) {
		printf("[0x%08x]%s", mem->read32(get_seg_adr(CS, eip + 1)), isDest?"\n\n":", ");
		return;
	}

	tip = eip;

	// <SIB>
	// 参考文献 (7)2-9 xxx インデックスがESPの時はなしになるらしい
	if (addrsize == size32 && rm == 4) {
		sib = mem->read8(get_seg_adr(CS, ++tip));
		idx = sib >> 3 & 7;
		base = sib & 7;
		sprintf(addressing_str[1][4],
			"[%s%s%s%s",
			(base != 5)? genreg_name[2][sib & 7] : mod? "EBP" : "",
			((base != 5 || mod) && idx != 4)? " + " : "",
			(idx != 4)? genreg_name[2][idx] :  "",
			(idx != 4)? sib_scale_str[sib >> 6] : "");
	}

	// + disp
	if (mod == 1) {
		sprintf(disp, " + 0x%02x", mem->read8(get_seg_adr(CS, ++tip)));
	} else if (mod == 2) {
		if (addrsize == size16) {
			sprintf(disp, " + 0x%04x", mem->read16(get_seg_adr(CS, ++tip)));
		} else {
			sprintf(disp, " + 0x%08x", mem->read32(get_seg_adr(CS, ++tip)));
		}
	} else {
		disp[0] = '\0';
	}
	printf("%s%s]%s", addressing_str[addrsize][rm], disp, isDest?"\n\n":", ");

	if (isReg && !isDest) {
		reg = modrm >> 3 & 7;
		printf("%s\n\n", genreg_name[regsize][reg]);
	}
}

#define DAS_pr(...) printf(__VA_ARGS__)

void CPU::DAS_prt_rmr_rrm(const char *s, bool isReg, bool isDest, REGSIZE regsize)
{
	u8 modrm;
	modrm = mem->read8(get_seg_adr(CS, eip));
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
	DAS_pr("%s ", s);
	DAS_modrm(modrm, isReg, isDest, regsize);
}
#else
#define DAS_dump_reg()
#define DAS_prt_post_op(n)
#define DAS_modrm(m, isR, isD, isW)
#define DAS_pr(...)
#define DAS_prt_rmr_rrm()
#endif // CORE_DAS

// modR/Mに続くディスプレースメントのバイト数を返す
u8 CPU::nr_disp_modrm(u8 modrm) {
	u8 mod, rm;

	mod = modrm >> 6;
	rm = modrm & 7;

	if (addrsize == size16) {
		if (mod == 1)
			return 1;
		else if (mod == 2)
			return 2;
		else if (mod == 3)
			return 0;

		if (mod == 0 && rm == 6) return 2;

		return 0;
	}

	u8 tmp;

	if (mod == 0)
		tmp = 0;
	else if (mod == 1)
		tmp = 1;
	else if (mod == 2)
		tmp = 4;
	else if (mod == 3)
		return 0;

	if (rm == 4)
		return tmp + 1;

	if (mod == 0 && rm == 5) return 4;

	return tmp;
}

// modが11でないことはあらかじめチェックしておくこと
// Effective Addressを取得
// eipはModR/Mの次をポイントしていなければならない
u16 CPU::modrm16_ea(u8 modrm)
{
	u16 mod, tmp16;

	mod = modrm >> 6;

	switch (modrm & 7) {
	case 0:
		tmp16 = bx + si;
		break;
	case 1:
		tmp16 = bx + di;
		break;
	case 2:
		tmp16 = bp + si;
		break;
	case 3:
		tmp16 = bp + di;
		break;
	case 4:
		tmp16 = si;
		break;
	case 5:
		tmp16 = di;
		break;
	case 6:
		if (mod == 0) {
			tmp16 = mem->read16(get_seg_adr(CS, eip));
			eip += 2;
			break;
		}
		tmp16 = bp;
		break;
	case 7:
		tmp16 = bx;
		break;
	}

	// xxx dispは符号つきなので考慮しなくてはならない
	if (mod == 1) {
		tmp16 += mem->read8(get_seg_adr(CS, eip));
		eip++;
	} else if (mod == 2) {
		tmp16 += mem->read16(get_seg_adr(CS, eip));
		eip += 2;
	}

	return tmp16;
}

u32 CPU::modrm32_ea(u8 modrm)
{
	u8 sib, idx, base;
	u16 mod;
	u32 tmp32;
	u32 sib_scale[] = {0, 2, 4, 8};

	mod = modrm >> 6;

	switch (modrm & 7) {
	case 0:
		tmp32 = eax;
		break;
	case 1:
		tmp32 = ecx;
		break;
	case 2:
		tmp32 = edx;
		break;
	case 3:
		tmp32 = ebx;
		break;
	case 4: // <SIB>
		sib = mem->read8(get_seg_adr(CS, eip++));
		idx = sib >> 3 & 7;
		base = sib & 7;
		tmp32 = (base == 5 && mod == 0)? 0 : genregd(base);
		if (idx != 4) {
			tmp32 += genregd(idx) * sib_scale[sib >> 6];
		}
		break;
	case 5:
		// [disp32]
		if (mod == 0) {
			tmp32 = mem->read32(get_seg_adr(CS, eip));
			eip += 4;
			break;
		}
		tmp32 = ebp;
		break;
	case 6:
		tmp32 = esi;
		break;
	case 7:
		tmp32 = edi;
		break;
	}

	// xxx dispは符号つきなので考慮しなくてはならない
	if (mod == 1) {
		tmp32 += mem->read8(get_seg_adr(CS, eip));
		eip++;
	} else if (mod == 2) {
		tmp32 += mem->read32(get_seg_adr(CS, eip));
		eip += 4;
	}

	return tmp32;
}

// modが11でないことはあらかじめチェックしておくこと
// Effective Addressを取得
// セグメント加算する
// eipはModR/Mの次をポイントしていなければならない
u32 CPU::modrm_seg_ea(u8 modrm)
{
	if (addrsize == size16) {
		return modrm16_ea(modrm)
			+ sdcr[modrm_add_seg[0][modrm >> 6][modrm & 7]].base;
	} else {
		// xxxSIBのベースがもしEBPだったらセグメントはCS?
		if ((modrm & 7) != 4) {
			return modrm32_ea(modrm) + sdcr[modrm_add_seg[1][modrm >> 6][modrm & 7]].base;
		} else {
			u8 sib;
			sib = get_seg_adr(CS, eip);
			return modrm32_ea(modrm) + sdcr[modrm_add_sib[sib & 7]].base;
		}
	}
}

// ModR/Mの値を返す(ModR/Mがソースになるパターン)
u32 CPU::modrmd(u8 modrm)
{
	if (modrm >> 6 == 3) {
		return genregd(modrm & 7);
	}
	return mem->read32(modrm_seg_ea(modrm));
}
u16 CPU::modrmw(u8 modrm)
{
	if (modrm >> 6 == 3) {
		return genregw(modrm & 7);
	}
	return mem->read16(modrm_seg_ea(modrm));
}
u8 CPU::modrmb(u8 modrm)
{
	if (modrm >> 6 == 3) {
		return *genregb[modrm & 7];
	}
	return mem->read8(modrm_seg_ea(modrm));
}

// - 引数aにセグメントを加算したアドレスを返却する
// - 386では、セグメント加算は、プロテクトモードでもリアルモードでも、
//   セグメントディスクリプターキャッシュ内のbase addressに対して行う
u32 CPU::get_seg_adr(const SEGREG seg, const u32 a) {
	return sdcr[seg].base + a;
}

void CPU::update_segreg(const u8 seg, const u16 n) {

	segreg[seg] = n;

	// リアルモードでは、SDCRのbaseを更新するだけ
	if (isRealMode) {
		sdcr[seg].base = n << 4;
		return;
	}

	// プロテクトモード
	u32 tmpadr, dst;

	// セグメントで指定されるディスクリプタの先頭アドレス
	tmpadr = gdtr.base + (n & 0xf8);
	dst = (mem->read32(tmpadr + 2) & 0x00ffffff)
		+ (mem->read8(tmpadr + 7) << 24);
	DAS_pr("base=0x%08x", dst);
	sdcr[seg].base = dst;

	dst = mem->read8(tmpadr + 5) + ((mem->read8(tmpadr + 6) & 0xf0) << 4);
	DAS_pr("\tAttr=0x%04x", (u16)dst);
	sdcr[seg].attr = (u16)dst;

	dst = mem->read16(tmpadr) + ((mem->read8(tmpadr + 6) & 0xf) << 16);
	if (sdcr[seg].attr & 0x800) {
		dst = dst << 12 | 0xfff;
	}
	DAS_pr("\tlimit=0x%08x\n", dst);
	sdcr[seg].limit = dst;

	if (seg != CS) {
		return;
	}

	// 参考文献(5)p.93 D(Default operation size)
	// DビットはCSデスクリプタでのみ意味があり、この設定はオーバーライド
	// プリフィックスでオーバーライドされる
	if (sdcr[seg].attr & 0x400) {
		opsize = size32;
		addrsize = size32;
	} else {
		opsize = size16;
		addrsize = size16;
	}
}

void CPU::exec() {
	u8 op, subop;
	u16 warg1, warg2;
	u32 darg1;
	u8 modrm, ndisp, sreg, greg, rm;
	u8 tmpb;
	u32 tmpadr;
	u32 src, dst, res;
	u32 cnt;
	s32 incdec;

#ifdef CORE_DAS
	char str8x[8][4] = {"ADD", "OR", "ADC", "SBB", "AND", "SUB", "", "CMP"};
	char strdx[8][8] = {"ROL", "ROR", "RCL", "RCR", "SHL/SAL", "SHR", "", "SAR"};
	char strf6[8][5] = {"TEST", "TEST", "NOT", "NEG", "MUL", "IMUL", "DIV", "IDIV"};
	char strfe[8][4] = {"INC", "DEC", "", "", "", "", "", ""};
	char strff[8][5] = {"INC", "DEC", "CALL", "CALL", "JMP", "JMP", "PUSH", ""};
#endif
	if (seg_ovride == 0 && !opsize_ovride && !addrsize_ovride &&!repe_prefix && !repne_prefix) {
		DAS_dump_reg();
	}

	// リアルモードでip++した時に16bitをこえて0に戻る場合を考慮して、
	// リアルモードの場合はeip++ではなくip++するようにした。
	// ここまでする考慮しなくてもよければ削る(高速化のため)
	op = mem->read8(get_seg_adr(CS, isRealMode? ip++ : eip++));
	DAS_pr("%08x %02x", get_seg_adr(CS, eip - 1), op);

	switch (op) {

#define readb read8
#define readw read16
#define readd read32
#define writeb write8
#define writew write16
#define writed write32

// マクロ内でのDAS_modrm()呼び出し時に使用
#define bword byte
#define wword word

#define bCAST u8
#define wCAST u16
#define dCAST u32

#define bALLF 0xff
#define wALLF 0xffff

#define bMSB1 0x80
#define wMSB1 0x8000

// OverFlag
#define OF_ADDb(r, s, d)			\
	(r^ s) & (r ^ d) & 0x80?		\
	  flagu8 |= OFSET8 : flagu8 &= ~OFSET8;

#define OF_ADDw(r, s, d)			\
	(r ^ s) & (r ^ d) & 0x8000?		\
	  flagu8 |= OFSET8 : flagu8 &= ~OFSET8;

#define OF_ADDd(r, s, d)
	// xxx ここは32bitは16bitとは違うフラグ計算

#define OF_ADCb OF_ADDb
#define OF_ADCw OF_ADDw
#define OF_ADCd OF_ADDd

#define OF_SUBb(r, s, d)			\
	(d^ r) & (d ^ s) & 0x80?		\
	  flagu8 |= OFSET8 : flagu8 &= ~OFSET8;

#define OF_SUBw(r, s, d)			\
	(d ^ r) & (d ^ s) & 0x8000?		\
	  flagu8 |= OFSET8 : flagu8 &= ~OFSET8;

#define OF_SUBd(r, s, d)
	// xxx ここは32bitは16bitとは違うフラグ計算

#define OF_SBBb OF_SUBb
#define OF_SBBw OF_SUBw
#define OF_SBBd OF_SUBd

#define FLAG8b(r, s, d, ANDN)		\
	flag8 = flag_calb[r ANDN];	\
	flag8 |= (d ^ s ^ r) & AF;

#define FLAG8w(r, s, d, ANDN)		\
	flag8 = flag_calw[r ANDN];	\
	flag8 |= (d ^ s ^ r) & AF;

#define FLAG8d(r, s, d, ANDN)
	// xxx ここは32bitは16bitとは違うフラグ計算


#define CAL_RM_R(OP, STR, BWD, CRY, ANDN)		\
	modrm = mem->read8(get_seg_adr(CS, eip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr(#STR" ");				\
	DAS_modrm(modrm, true, false, BWD##word);	\
	eip++;						\
	src = genreg##BWD(modrm >> 3 & 7);		\
	if ((modrm & 0xc0) == 0xc0) {			\
		dst = genreg##BWD(modrm & 7);		\
		res = dst OP src + CRY;			\
		genreg##BWD(modrm & 7) = (BWD##CAST)res;\
	} else {					\
		tmpadr = modrm_seg_ea(modrm);		\
		dst = mem->read##BWD(tmpadr);		\
		res = dst OP src + CRY;			\
		mem->write##BWD(tmpadr, (BWD##CAST)res);\
	}						\
	FLAG8##BWD(res, src, dst, ANDN);		\
	OF_##STR##BWD(res, src, dst);

#define CAL_R_RM(OP, STR, BWD, CRY, ANDN)		\
	modrm = mem->read8(get_seg_adr(CS, eip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr(#STR" ");				\
	DAS_modrm(modrm, true, true, BWD##word);	\
	eip++;						\
	dst = genreg##BWD(modrm >> 3 & 7);		\
	src = modrm##BWD(modrm);			\
	res = dst OP src + CRY;				\
	genreg##BWD(modrm >> 3 & 7) = (BWD##CAST)res;	\
	FLAG8##BWD(res, src, dst, ANDN);		\
	OF_##STR##BWD(res, src, dst);

/******************** ADD ********************/
/*
+--------+-----------+---------+---------+
|000000dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
OF/SF/ZF/AF/PF/CF:結果による
 */
	case 0x00: // ADD r/m8, r8
		CAL_RM_R(+, ADD, b, 0, );
		break;
	case 0x01: // ADD r/m16, r16 (ADD r/m32, r32)
		if (opsize == size16) {
			CAL_RM_R(+, ADD, w, 0, );
		} else {
			CAL_RM_R(+, ADD, d, 0, );
		}
		break;
	case 0x02: // ADD r8, r/m8
		CAL_R_RM(+, ADD, b, 0, );
		break;
	case 0x03: // ADD r16, r/m16 (ADD r32, r/m32)
		if (opsize == size16) {
			CAL_R_RM(+, ADD, w, 0, );
		} else {
			CAL_R_RM(+, ADD, d, 0, );
		}
		break;
	case 0x04: // ADD AL, imm8
		DAS_prt_post_op(1);
		src = mem->read8(get_seg_adr(CS, eip));
		DAS_pr("ADD AL, 0x%02x\n\n", src);
		eip++;
		res = al + src;
		FLAG8b(res, src, al, );
		OF_ADDb(res, src, al);
		al = res;
		break;
	case 0x05: // ADD AX, imm16 (ADD EAX, imm32)
		if (opsize == size16) {
			DAS_prt_post_op(2);
			src = mem->read16(get_seg_adr(CS, eip));
			DAS_pr("ADD AX, 0x%04x\n\n", src);
			eip += 2;
			res = ax + src;
			FLAG8w(res, src, ax, );
			OF_ADDw(res, src, ax);
			ax = res;
		}else {
			DAS_prt_post_op(4);
			src = mem->read32(get_seg_adr(CS, eip));
			DAS_pr("ADD EAX, 0x%08x\n\n", src);
			eip += 4;
			res = eax + src;
			FLAG8d(res, src, eax, );
			OF_ADDd(res, src, eax);
			eax = res;
		}
		break;

/******************** ADC ********************/

	case 0x10: // ADC r/m8, r8
		CAL_RM_R(+, ADC, b, (flag8 & CF), );
		break;
	case 0x11: // ADC r/m16, r16 (ADC r/m32, r32)
		if (opsize == size16) {
			CAL_RM_R(+, ADC, w, (flag8 & CF), );
		} else {
			CAL_RM_R(+, ADC, d, (flag8 & CF), );
		}
		break;
	case 0x12: // ADC r8, r/m8
		CAL_R_RM(+, ADC, b, (flag8 & CF), );
		break;
	case 0x13: // ADC r16, r/m16 (ADC r32, r/m32)
		CAL_R_RM(+, ADC, w, (flag8 & CF), );
		break;
	case 0x14: // ADC AL, imm8
		DAS_prt_post_op(1);
		src = mem->read8(get_seg_adr(CS, eip));
		DAS_pr("ADC AL, 0x%02x\n\n", src);
		eip++;
		res = al + src + (flag8 & CF);
		FLAG8b(res, src, al, );
		OF_ADCb(res, src, al);
		al = res;
		break;
	case 0x15: // ADC AX, imm16 (ADC EAX, imm32)
		if (opsize == size16) {
			DAS_prt_post_op(2);
			src = mem->read16(get_seg_adr(CS, eip));
			DAS_pr("ADC AX, 0x%04x\n\n", src);
			eip += 2;
			res = ax + src + (flag8 & CF);
			FLAG8w(res, src, ax, );
			OF_ADCw(res, src, ax);
			ax = res;
		} else {
			DAS_prt_post_op(4);
			src = mem->read32(get_seg_adr(CS, eip));
			DAS_pr("ADC EAX, 0x%08x\n\n", src);
			eip += 4;
			res = eax + src + (flag8 & CF);
			FLAG8d(res, src, eax, );
			OF_ADCd(res, src, eax);
			eax = res;
		}
		break;

/******************** OR ********************/
/*
+--------+-----------+---------+---------+
|000010dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
OF/CF:クリア, SF/ZF/PF:結果による, AF:不定
 */

#define FLAG_LOGOPb(d)		\
	flag8 = flag_calb[d];	\
	flagu8 &= ~OFSET8;
#define FLAG_LOGOPw(d)		\
	flag8 = flag_calw[d];	\
	flagu8 &= ~OFSET8;
#define FLAG_LOGOPd(d)		\
	/* xxx */;

// LOGical OPeration (OP r, r/m)
#define LOGOP_R_RM(OP, STR, BWD)			\
	modrm = mem->read8(get_seg_adr(CS, eip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr(#STR" ");				\
	DAS_modrm(modrm, true, true, BWD##word);	\
	eip++;						\
	greg = modrm >> 3 & 7;				\
	dst = genreg##BWD(greg);			\
	dst OP##= modrm##BWD(modrm);			\
	genreg##BWD(greg) = dst;			\
	FLAG_LOGOP##BWD(dst)

// LOGical OPeration (OP r/m, r)
#define LOGOP_RM_R(OP, STR, BWD)			\
	modrm = mem->read8(get_seg_adr(CS, eip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr(#STR" ");				\
	DAS_modrm(modrm, true, false, BWD##word);	\
	rm = modrm & 7;					\
	eip++;						\
	src = genreg##BWD(modrm >> 3 & 7);		\
	if ((modrm & 0xc0) == 0xc0) {			\
		dst = genreg##BWD(rm);			\
		dst OP##= src;				\
		genreg##BWD(rm) = (BWD##CAST)dst;	\
	} else {					\
		tmpadr = modrm_seg_ea(modrm);		\
		dst = mem->read##BWD(tmpadr);		\
		dst OP##= src;				\
		mem->write##BWD(tmpadr, (BWD##CAST)dst);\
	}						\
	FLAG_LOGOP##BWD(dst)

	case 0x08: // OR r/m8, r8
		LOGOP_RM_R(|, OR, b);
		break;
	case 0x09: // OR r/m16, r16 (OR r/m32, r32)
		if (opsize == size16) {
			LOGOP_RM_R(|, OR, w);
		} else {
			LOGOP_RM_R(|, OR, d);
		}
		break;
	case 0x0a: // OR r8, r/m8
		LOGOP_R_RM(|, OR, b);
		break;
	case 0x0b: // OR r16, r/m16 (OR r32, r/m32)
		if (opsize == size16) {
			LOGOP_R_RM(|, OR, w);
		} else {
			LOGOP_R_RM(|, OR, d);
		}
		break;
	case 0x0c: // OR AL, imm8
		DAS_prt_post_op(1);
		src = mem->read8(get_seg_adr(CS, eip));
		DAS_pr("OR AL, 0x%02x\n\n", src);
		al |= src;
		FLAG_LOGOPb(al);
		eip++;
		break;
	case 0x0d: // OR AX, imm16 (OR EAX, imm32)
		if (opsize == size16) {
			DAS_prt_post_op(2);
			src = mem->read16(get_seg_adr(CS, eip));
			DAS_pr("OR AX, 0x%04x\n\n", src);
			ax |= src;
			FLAG_LOGOPw(ax);
			eip += 2;
		} else {
			DAS_prt_post_op(4);
			src = mem->read32(get_seg_adr(CS, eip));
			DAS_pr("OR EAX, 0x%08x\n\n", src);
			eax |= src;
			FLAG_LOGOPd(eax);
			eip += 4;
		}
		break;

/******************** PUSH ********************/
// xxxセグメントオーバーライドされていても、call, pusha, enterではSSを使うらしい
#define PUSHW0(d)					\
	sp -= 2;					\
	mem->write16((segreg[SS] << 4) + sp, (u16)(d));

#define PUSHD0(d)				\
	esp -= 4;				\
	mem->write32((segreg[SS] << 4) + esp, d);

#define PUSHW(d)				\
	sp -= 2;				\
	mem->write16(get_seg_adr(SS, sp), d);

#define PUSHD(d)				\
	esp -= 4;				\
	mem->write32(get_seg_adr(SS, esp), d);

#define PUSHW_GENREG(reg)		\
	DAS_prt_post_op(0);		\
	DAS_pr("PUSH "#reg"\n\n");	\
	PUSHW(reg)

#define PUSHD_GENREG(reg)		\
	DAS_prt_post_op(0);		\
	DAS_pr("PUSH "#reg"\n\n");	\
	PUSHD(reg)

#define PUSH_SEG(seg)			\
	DAS_prt_post_op(0);		\
	DAS_pr("PUSH "#seg"\n\n");	\
	PUSHW(segreg[seg])

/* 2バイト命令 (PUSH FS/GS用) */
#define PUSH_SEG2(seg)			\
	DAS_prt_post_op(1);		\
	DAS_pr("PUSH "#seg"\n\n");	\
	PUSHW(segreg[seg]);		\
	eip++

	case 0x06: // PUSH ES
		PUSH_SEG(ES);
		break;
	case 0x0e: // PUSH CS
		PUSH_SEG(CS);
		break;
	case 0x16: // PUSH SS
		PUSH_SEG(SS);
		break;
	case 0x1e: // PUSH DS
		PUSH_SEG(DS);
		break;

	case 0x50: // PUSH AX (PUSH EAX)
		if (opsize == size16) {
			PUSHW_GENREG(ax);
		} else {
			PUSHD_GENREG(eax);
		}
		break;
	case 0x51: // PUSH CX (PUSH ECX)
		if (opsize == size16) {
			PUSHW_GENREG(cx);
		} else {
			PUSHD_GENREG(ecx);
		}
		break;
	case 0x52: // PUSH DX (PUSH EDX)
		if (opsize == size16) {
			PUSHW_GENREG(dx);
		} else {
			PUSHD_GENREG(edx);
		}
		break;
	case 0x53: // PUSH BX (PUSH EBX)
		if (opsize == size16) {
			PUSHW_GENREG(bx);
		} else {
			PUSHD_GENREG(ebx);
		}
		break;
	case 0x54: // PUSH SP (PUSH ESP)
		if (opsize == size16) {
			PUSHW_GENREG(sp);
		} else {
			PUSHD_GENREG(esp);
		}
		break;
	case 0x55: // PUSH BP (PUSH EBP)
		if (opsize == size16) {
			PUSHW_GENREG(bp);
		} else {
			PUSHD_GENREG(ebp);
		}
		break;
	case 0x56: // PUSH SI (PUSH ESI)
		if (opsize == size16) {
			PUSHW_GENREG(si);
		} else {
			PUSHD_GENREG(esi);
		}
		break;
	case 0x57: // PUSH DI (PUSH EDI)
		if (opsize == size16) {
			PUSHW_GENREG(di);
		} else {
			PUSHD_GENREG(edi);
		}
		break;

	case 0x60: // PUSHA (PUSHAD)
		DAS_prt_post_op(0);
		if (opsize == size16) {
			DAS_pr("PUSHA\n\n");
			dst = sp;
			PUSHW0(ax);
			PUSHW0(cx);
			PUSHW0(dx);
			PUSHW0(bx);
			PUSHW0(dst);
			PUSHW0(bp);
			PUSHW0(si);
			PUSHW0(di);
		} else {
			DAS_pr("PUSHAD\n\n");
			dst = esp;
			PUSHD0(eax);
			PUSHD0(ecx);
			PUSHD0(edx);
			PUSHD0(ebx);
			PUSHD0(dst);
			PUSHD0(ebp);
			PUSHD0(esi);
			PUSHD0(edi);
		}
		break;

	case 0x68: // PUSH imm16 (PUSH imm32)
		if (opsize == size16) {
			DAS_prt_post_op(2);
			dst = mem->read16(get_seg_adr(CS, eip));
			DAS_pr("PUSH 0x%04x\n\n", dst);
			PUSHW(dst);
			eip += 2;
		} else {
			DAS_prt_post_op(4);
			dst = mem->read32(get_seg_adr(CS, eip));
			DAS_pr("PUSH 0x%08x\n\n", dst);
			PUSHD(dst);
			eip += 4;
		}
		break;

	case 0x9c: // PUSHF (PUSHFD)
		DAS_prt_post_op(0);
		if (opsize == size16) {
			DAS_pr("PUSHF\n\n");
			PUSHW(flagu8 << 8 | flag8);
		} else {
			DAS_pr("PUSHFD\n\n");
			PUSHD(flagu8 << 8 | flag8);
			// xxx ちゃんとやる
		}
		break;

/******************** POP ********************/
// xxxセグメントオーバーライドされていても、call, pusha, enterではSSを使うらしい
#define POPW0(d)				\
	d = mem->read16((segreg[SS] << 4) + sp);\
	sp += 2;

#define POPD0(d)				\
	d = mem->read32((segreg[SS] << 4) + esp);\
	sp += 4;

#define POPW(d)					\
	d = mem->read16(get_seg_adr(SS, sp));	\
	sp += 2;

#define POPD(d)					\
	d = mem->read32(get_seg_adr(SS, esp));	\
	sp += 4;

#define POPW_GENREG(reg)		\
	DAS_prt_post_op(0);		\
	DAS_pr("POP "#reg"\n\n");	\
	POPW(reg)

#define POPD_GENREG(reg)		\
	DAS_prt_post_op(0);		\
	DAS_pr("POP "#reg"\n\n");	\
	POPD(reg)

#define POP_SEG(seg)			\
	DAS_prt_post_op(0);		\
	DAS_pr("POP "#seg"\n\n");	\
	POPW(dst);			\
	update_segreg(seg, (u16)dst)

/* 2バイト命令 (PUSH FS/GS用) */
#define POP_SEG2(seg)			\
	DAS_prt_post_op(1);		\
	DAS_pr("POP "#seg"\n\n");	\
	POPW(dst);			\
	update_segreg(seg, (u16)dst);	\
	eip++

	case 0x07: // POP ES
		POP_SEG(ES);
		break;
	case 0x17: // POP SS
		POP_SEG(SS);
		break;
	case 0x1f: // POP DS
		POP_SEG(DS);
		break;

	case 0x58: // POP AX (POP EAX)
		if (opsize == size16) {
			POPW_GENREG(ax);
		} else {
			POPD_GENREG(eax);
		}
		break;
	case 0x59: // POP CX (POP ECX)
		if (opsize == size16) {
			POPW_GENREG(cx);
		} else {
			POPD_GENREG(ecx);
		}
		break;
	case 0x5a: // POP DX (POP EDX)
		if (opsize == size16) {
			POPW_GENREG(dx);
		} else {
			POPD_GENREG(edx);
		}
		break;
	case 0x5b: // POP BX (POP EBX)
		if (opsize == size16) {
			POPW_GENREG(bx);
		} else {
			POPD_GENREG(ebx);
		}
		break;
	case 0x5c: // POP SP (POP ESP)
		if (opsize == size16) {
			POPW_GENREG(sp);
		} else {
			POPD_GENREG(esp);
		}
		break;
	case 0x5d: // POP BP (POP EBP)
		if (opsize == size16) {
			POPW_GENREG(bp);
		} else {
			POPD_GENREG(ebp);
		}
		break;
	case 0x5e: // POP SI (POP ESI)
		if (opsize == size16) {
			POPW_GENREG(si);
		} else {
			POPD_GENREG(esi);
		}
		break;
	case 0x5f: // POP DI (POP EDI)
		if (opsize == size16) {
			POPW_GENREG(di);
		} else {
			POPD_GENREG(edi);
		}
		break;

	case 0x61: // POPA (POPAD)
		DAS_prt_post_op(0);
		if (opsize == size16) {
			DAS_pr("POPA\n\n");
			POPW0(di);
			POPW0(si);
			POPW0(bp);
			sp += 2;
			POPW0(bx);
			POPW0(dx);
			POPW0(cx);
			POPW0(ax);
		} else {
			DAS_pr("POPAD\n\n");
			POPD0(edi);
			POPD0(esi);
			POPD0(ebp);
			sp += 4;
			POPD0(ebx);
			POPD0(edx);
			POPD0(ecx);
			POPD0(eax);
		}
		break;

/*
+--------+-----------+---------+---------+
|10001111|mod 000 r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
 */
		// xxx POP r16とPOP m16の違いは？
	case 0x8f: // POP m16 (POP m32)
		if (opsize == size16) {
			modrm = mem->read8(get_seg_adr(CS, eip));
			DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
			DAS_pr("POP ");
			DAS_modrm(modrm, false, true, word);
			eip++;
			if ((modrm & 0xc0) == 0xc0) {
				genregw(modrm & 7) = mem->read16(get_seg_adr(SS, sp));
			} else {
				mem->write16(modrm_seg_ea(modrm),
					     mem->read16(get_seg_adr(SS, sp)));
			}
			sp += 2;
		} else {
			modrm = mem->read8(get_seg_adr(CS, eip));
			DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
			DAS_pr("POP ");
			DAS_modrm(modrm, false, true, dword);
			eip++;
			if ((modrm & 0xc0) == 0xc0) {
				genregd(modrm & 7) = mem->read32(get_seg_adr(SS, esp));
			} else {
				mem->write32(modrm_seg_ea(modrm),
					     mem->read32(get_seg_adr(SS, esp)));
			}
			sp += 4;
		}
		break;

	case 0x9d: // POPF (POPFD)
		DAS_prt_post_op(0);
		if (opsize == size16) {
			DAS_pr("POPF\n\n");
			POPW(dst);
			flagu8 = (u8)(dst >> 8);
			flag8  = dst & 0xff;
		} else {
			DAS_pr("POPFD\n\n");
			POPD(dst);
			flagu8 = (u8)(dst >> 8);
			flag8  = dst & 0xff;
			// xxx ちゃんとやる
		}
		break;

/******************** AND ********************/
/*
+--------+-----------+---------+---------+
|001000dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
OF/CF:クリア, SF/ZF/PF:結果による, AF:不定
 */

	case 0x20: // AND r/m8, r8
		LOGOP_RM_R(&, AND, b);
		break;
	case 0x21: // AND r/m16, r16 (AND r/m32, r32)
		if (opsize == size16) {
			LOGOP_RM_R(&, AND, w);
		} else {
			LOGOP_RM_R(&, AND, d);
		}
		break;
	case 0x22: // AND r8, r/m8
		LOGOP_R_RM(&, AND, b);
		break;
	case 0x23: // AND r16, r/m16 (AND r32, r/m32)
		if (opsize == size16) {
			LOGOP_R_RM(&, AND, w);
		} else {
			LOGOP_R_RM(&, AND, d);
		}
		break;
/*
+--------+--------+-------------+
|0010010w|  data  |(data if w=1)|
+--------+--------+-------------+
OF/CF:クリア, SF/ZF/PF:結果による, AF:不定
 */
	case 0x24: // and al, imm8
		DAS_prt_post_op(1);
		DAS_pr("AND AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, eip)));
		al &= mem->read8(get_seg_adr(CS, eip++));
		flag8 = flag_calb[al];
		flagu8 &= ~OFSET8;
		break;
	case 0x25: // AND AX, imm16 (AND EAX, imm32)
		DAS_prt_post_op(opsize == size16?2:4);
		DAS_pr((opsize == size16)?"AND AX, 0x%04x\n\n":"AND EAX, 0x%08x\n\n", (opsize == size16)?mem->read16(get_seg_adr(CS, eip)):mem->read32(get_seg_adr(CS, eip)));
		if (opsize == size16) {
			ax &= mem->read16(get_seg_adr(CS, eip));
			flag8 = flag_calw[ax];
			flagu8 &= ~OFSET8;
			eip += 2;
		} else {
			eax &= mem->read32(get_seg_adr(CS, eip));
			// xxx flag
			eip += 4;
		}
		break;

/******************** DAA/DAS/AAA/AAS/AAM/AAD ********************/

	case 0x27:
		DAS_prt_post_op(0);
		DAS_pr("DAA\n\n");
		tmpb = flag8 & CF;
		res = al;
		if (flag8 & AF || (al & 0x0f) > 9) {
			res += 6;
			flag8 = AF; // AF以外はリセット
		} else {
			flag8 = 0; // AFも含めてリセット
		}
		if (tmpb || al > 0x99) { // ここの比較はresでなくalで正しい
			res += 0x60;
			flag8 |= CF;
		}
		al = (u8)res;
		flag8 |= flag_calb[al]; // CFは確定しているの判定は8bitで行う
		break;
	case 0x2f:
		DAS_prt_post_op(0);
		DAS_pr("DAS\n\n");
		tmpb = flag8 & CF;
		res = al;
		if (flag8 & AF || (al & 0x0f) > 9) {
			res -= 6;
			flag8 = AF;
		} else {
			flag8 = 0;
		}
		if (tmpb || al > 0x99) {
			res -= 0x60;
			flag8 |= CF;
		}
		al = (u8)res;
		flag8 |= flag_calb[al];
		break;
	case 0x37:
		DAS_prt_post_op(0);
		DAS_pr("AAA\n\n");
		if (flag8 & AF || (al & 0x0f) > 9) {
			al = (al + 6) & 0x0f;
			ah++;
			flag8 = AF & CF; // AF, CF以外はリセット(他は未定義)
		} else {
			flag8 = 0; // CF, AFも含めてリセット(他は未定義)
		}
		break;
	case 0x3f:
		DAS_prt_post_op(0);
		DAS_pr("AAS\n\n");
		if (flag8 & AF || (al & 0x0f) > 9) {
			al = (al - 6) & 0x0f;
			ah--;
			flag8 = AF & CF;
		} else {
			flag8 = 0;
		}
		break;
	case 0xd4:
		DAS_prt_post_op(1);
		tmpb = mem->read8(get_seg_adr(CS, eip));
		if (tmpb != 0x0a) {
			// imm8が0x0aでなければ本当はミーモニックなし
			DAS_pr("AAM 0x%02x\n\n", tmpb);
		} else {
			DAS_pr("AAM\n\n");
		}
		eip++;
		ah = al / tmpb;
		al = al % tmpb;
		flag8 = flag_calb[al]; // CFは未定義(OF, AFも未定義)
		break;
	case 0xd5:
		DAS_prt_post_op(1);
		tmpb = mem->read8(get_seg_adr(CS, eip));
		if (tmpb != 0x0a) {
			DAS_pr("AAD 0x%02x\n\n", tmpb);
		} else {
			DAS_pr("AAD\n\n");
		}
		eip++;
		al += (ah + tmpb) & 0xff;
		ah = 0;
		flag8 = flag_calb[al];
		break;

/******************** SBB ********************/

	case 0x18: // SBB r/m8, r8
		CAL_RM_R(-, SBB, b, -(flag8 & CF), & 0x1ff);
		break;
	case 0x19: // SBB r/m16, r16 (SBB r/m32, r32)
		if (opsize == size16) {
			CAL_RM_R(-, SBB, w, -(flag8 & CF), & 0x1ffff);
		} else {
			CAL_RM_R(-, SBB, d, -(flag8 & CF), & 0x1ffff);
		}
		break;
	case 0x1a: // SBB r8, r/m8
		CAL_R_RM(-, SBB, b, -(flag8 & CF), & 0x1ff);
		break;
	case 0x1b: // SBB r16, r/m16 (SBB r32, r/m32)
		if (opsize == size16) {
			CAL_R_RM(-, SBB, w, -(flag8 & CF), & 0x1ffff);
		} else {
			CAL_R_RM(-, SBB, d, -(flag8 & CF), & 0x1ffff);
		}
		break;
	case 0x1c: // SBB AL, imm8
		DAS_prt_post_op(1);
		DAS_pr("SBB AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, eip)));
		dst = al;
		src = mem->read8(get_seg_adr(CS, eip));
		res = dst - src - (flag8 & CF);
		al = (u8)res;
		eip ++;
		FLAG8b(res, src, dst, & 0x1ff);
		OF_SBBb(res, src, dst);
		break;
	case 0x1d: // SBB AX, imm16 (SBB EAX, imm32)
		if (opsize == size16) {
			DAS_prt_post_op(2);
			src = mem->read16(get_seg_adr(CS, eip));
			DAS_pr("SBB AX, 0x%04x\n\n", src);
			dst = ax;
			res = dst - src - (flag8 & CF);
			ax = (u16)res;
			eip += 2;
			FLAG8w(res, src, dst, & 0x1ffff);
			OF_SBBw(res, src, dst);
		} else {
			DAS_prt_post_op(4);
			src = mem->read32(get_seg_adr(CS, eip));
			DAS_pr("SBB EAX, 0x%08x\n\n", src);
			dst = eax;
			res = dst - src - (flag8 & CF);
			eax = res;
			eip += 4;
			FLAG8d(res, src, dst, & 0x1ffff);
			OF_SBBd(res, src, dst);
		}
		break;

/******************** SUB ********************/

	case 0x28: // SUB r/m8, r8
		CAL_RM_R(-, SUB, b, 0, & 0x1ff);
		break;
	case 0x29: // SUB r/m16, r16 (SUB r/m32, r32)
		if (opsize == size16) {
			CAL_RM_R(-, SUB, w, 0, & 0x1ffff);
		} else {
			CAL_RM_R(-, SUB, d, 0, & 0x1ffff);
		}
		break;
	case 0x2a: // SUB r8, r/m8
		CAL_R_RM(-, SUB, b, 0, & 0x1ff);
		break;
	case 0x2b: // SUB r16, r/m16 (SUB r32, r/m32)
		if (opsize == size16) {
			CAL_R_RM(-, SUB, w, 0, & 0x1ffff);
		} else {
			CAL_R_RM(-, SUB, d, 0, & 0x1ffff);
		}
		break;
	case 0x2c: // SUB AL, imm8
		DAS_prt_post_op(1);
		src = mem->read8(get_seg_adr(CS, eip));
		DAS_pr("SUB AL, 0x%02x\n\n", src);
		dst = al;
		res = dst - src;
		al = (u8)res;
		eip ++;
		FLAG8b(res, src, dst, & 0x1ff);
		OF_SUBb(res, src, dst);
		break;
	case 0x2d: // SUB AX, imm16 (SUB EAX, imm32)
		if (opsize == size16) {
			DAS_prt_post_op(2);
			src = mem->read16(get_seg_adr(CS, eip));
			DAS_pr("SUB AX, 0x%04x\n\n", src);
			dst = ax;
			res = dst - src;
			ax = (u16)res;
			eip += 2;
			FLAG8w(res, src, dst, & 0x1ffff);
			OF_SUBw(res, src, dst);
		} else {
			DAS_prt_post_op(4);
			src = mem->read32(get_seg_adr(CS, eip));
			DAS_pr("SUB EAX, 0x%08x\n\n", src);
			dst = eax;
			res = dst - src;
			eax = res;
			eip += 4;
			FLAG8d(res, src, dst, & 0x1ffff);
			OF_SUBd(res, src, dst);
		}
		break;

/******************** XOR ********************/
/*
          76  543 210
+--------+-----------+---------+---------+
|001100dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
OF/CF:クリア, SF/ZF/PF:結果による, AF:不定
 */

	case 0x30: // XOR r/m8, r8
		LOGOP_RM_R(^, XOR, b);
		break;
	case 0x31: // XOR r/m16, r16 (XOR r/m32, r32)
		if (opsize == size16) {
			LOGOP_RM_R(^, XOR, w);
		} else {
			LOGOP_RM_R(^, XOR, d);
		}
		break;
	case 0x32: // XOR r8, r/m8
		LOGOP_R_RM(^, XOR, b);
		break;
	case 0x33: // XOR r16, r/m16 (xxx XOR r32, r/m32)
		if (opsize == size16) {
			LOGOP_R_RM(^, XOR, w);
		} else {
			LOGOP_R_RM(^, XOR, d);
		}
		break;
	case 0x34: // XOR AL, imm8
		DAS_prt_post_op(1);
		DAS_pr("XOR AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, eip)));
		al ^= mem->read8(get_seg_adr(CS, eip));
		eip++;
		flag8 = flag_calb[al];
		flagu8 &= ~OFSET8;
		break;
	case 0x35: // XOR AX, imm16 (XOR EAX, imm32)
		if (opsize == size16) {
			DAS_prt_post_op(2);
			src = mem->read16(get_seg_adr(CS, eip));
			DAS_pr("XOR AX, 0x%04x\n\n", src);
			ax ^= src;
			eip += 2;
			flag8 = flag_calw[ax];
			flagu8 &= ~OFSET8;
		} else {
			DAS_prt_post_op(4);
			src = mem->read32(get_seg_adr(CS, eip));
			DAS_pr("XOR EAX, 0x%08x\n\n", src);
			eax ^= src;
			eip += 4;
			/* xxx flag */
			flagu8 &= ~OFSET8;
		}
		break;

/******************** CMP ********************/

#define CMP_R_RM(BWD)					\
	modrm = mem->read8(get_seg_adr(CS, eip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr("CMP ");					\
	DAS_modrm(modrm, true, true, BWD##word);	\
	eip++;						\
	dst = genreg##BWD(modrm >> 3 & 7);		\
	src = modrm##BWD(modrm);			\
	res = dst - src;				\
	flag8 = flag_cal##BWD[dst];			\
	/* xxx AFの計算が必要 */			\
	flagu8 &= ~OFSET8;


#define CMP_RM_R(BWD)					\
	modrm = mem->read8(get_seg_adr(CS, eip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr("CMP ");					\
	DAS_modrm(modrm, true, false, BWD##word);	\
	rm = modrm & 7;					\
	eip++;						\
	src = genreg##BWD(modrm >> 3 & 7);		\
	if ((modrm & 0xc0) == 0xc0) {			\
		dst = genreg##BWD(rm);			\
		res = dst - src;			\
	} else {					\
		tmpadr = modrm_seg_ea(modrm);		\
		dst = mem->read##BWD(tmpadr);		\
		res = dst - src;			\
	}						\
	flag8 = flag_cal##BWD[dst];			\
	/* xxx AFの計算が必要 */			\
	flagu8 &= ~OFSET8;

/*
3C ib
CF/OF/SF/ZF/AF/PF:結果による
*/
	case 0x38: // CMP r/m8, r8
		CMP_RM_R(b);
		break;
	case 0x39: // CMP r/m16, r16 (CMP r/m32, r32)
		CMP_RM_R(w);
		break;
	case 0x3A: // CMP r8, r/m8
		CMP_R_RM(b);
		break;
	case 0x3B: // CMP r16, r/m16 (CMP r32, r/m32)
		CMP_R_RM(w);
		break;
	case 0x3c: // CMP AL, imm8
		DAS_prt_post_op(1);
		src = mem->read8(get_seg_adr(CS, eip));
		DAS_pr("CMP AL, 0x%02x\n\n", src);
		res = al - src;
		eip++;
		flag8 = flag_calb[res & 0x1ff];
		flag8 |= (al ^ src ^ res) & AF;
		(al ^ res) & (al ^ src) & 0x80?
			flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
		break;
	case 0x3d: // CMP AX, imm16 (CMP EAX, imm32)
		DAS_prt_post_op(2);
		src = mem->read16(get_seg_adr(CS, eip));
		DAS_pr("CMP AX, 0x%04x\n\n", src);
		res = ax - src;
		eip += 2;
		flag8 = flag_calw[res & 0x1ffff];
		flag8 |= (ax ^ src ^ res) & AF;
		(ax ^ res) & (ax ^ src) & 0x8000?
			flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
		break;


/******************** INC ********************/
/*
CF:影響なし, OF/SF/ZF/AF/PF:結果による
 */
// xxx OFの計算がNP2と違う
#define INC_R16(reg)						\
	DAS_prt_post_op(0);					\
	DAS_pr("INC %s\n\n", #reg);				\
	dst = reg;						\
	reg++;							\
	flag8 &= CF; /* CF以外はリセット*/			\
	flag8 |= flag_calw[reg];				\
	flag8 |= (dst ^ reg) & AF;				\
	(dst ^ reg) & 0x8000?flagu8 |= OFSET8:flagu8 &= ~OFSET8

#define INC_R32(reg)						\
	DAS_prt_post_op(0);					\
	DAS_pr("INC %s\n\n", #reg);				\
	dst = reg;						\
	reg++;							\
	flag8 &= CF; /* CF以外はリセット*/			\
	/* xxx flag */

	case 0x40: // INC AX (INC EAX)
		if (opsize == size16) {
			INC_R16(ax);
		} else {
			INC_R32(eax);
		}
		break;
	case 0x41: // INC CX (INC ECX)
		if (opsize == size16) {
			INC_R16(cx);
		} else {
			INC_R32(ecx);
		}
		break;
	case 0x42: // INC DX (INC EDX)
		if (opsize == size16) {
			INC_R16(dx);
		} else {
			INC_R32(edx);
		}
		break;
	case 0x43: // INC BX (INC EBX)
		if (opsize == size16) {
			INC_R16(bx);
		} else {
			INC_R32(ebx);
		}
		break;
	case 0x44: // INC SP (INC ESP)
		if (opsize == size16) {
			INC_R16(sp);
		} else {
			INC_R32(esp);
		}
		break;
	case 0x45: // INC BP (INC EBP)
		if (opsize == size16) {
			INC_R16(bp);
		} else {
			INC_R32(ebp);
		}
		break;
	case 0x46: // INC SI (INC ESI)
		if (opsize == size16) {
			INC_R16(si);
		} else {
			INC_R32(esi);
		}
		break;
	case 0x47: // INC DI (INC EDI)
		if (opsize == size16) {
			INC_R16(di);
		} else {
			INC_R32(edi);
		}
		break;

/******************** DEC ********************/
/*
CF:影響なし, OF/SF/ZF/AF/PF:結果による
 */
// xxx OFの計算がNP2と違う
#define DEC_R16(reg)						\
	DAS_prt_post_op(0);					\
	DAS_pr("DEC %s\n\n", #reg);				\
	dst = reg;						\
	reg--;							\
	flag8 &= CF; /* CF以外はリセット*/			\
	flag8 |= flag_calw[reg];				\
	flag8 |= (dst ^ reg) & AF;				\
	(dst ^ reg) & 0x8000?flagu8 |= OFSET8:flagu8 &= ~OFSET8

#define DEC_R32(reg)						\
	DAS_prt_post_op(0);					\
	DAS_pr("DEC %s\n\n", #reg);				\
	dst = reg;						\
	reg--;							\
	flag8 &= CF; /* CF以外はリセット*/			\
	/* xxx flag */

	case 0x48: // DEC AX (DEC EAX)
		if (opsize  == size16) {
			DEC_R16(ax);
		} else {
			DEC_R32(eax);
		}
		break;
	case 0x49: // DEC CX (DEC ECX)
		if (opsize  == size16) {
			DEC_R16(cx);
		} else {
			DEC_R32(ecx);
		}
		break;
	case 0x4a: // DEC DX (DEC EDX)
		if (opsize  == size16) {
			DEC_R16(dx);
		} else {
			DEC_R32(edx);
		}
		break;
	case 0x4b: // DEC BX (DEC EBX)
		if (opsize  == size16) {
			DEC_R16(bx);
		} else {
			DEC_R32(ebx);
		}
		break;
	case 0x4c: // DEC SP (DEC ESP)
		if (opsize  == size16) {
			DEC_R16(sp);
		} else {
			DEC_R32(esp);
		}
		break;
	case 0x4d: // DEC BP (DEC EBP)
		if (opsize  == size16) {
			DEC_R16(bp);
		} else {
			DEC_R32(ebp);
		}
		break;
	case 0x4e: // DEC SI (DEC ESI)
		if (opsize  == size16) {
			DEC_R16(si);
		} else {
			DEC_R32(esi);
		}
		break;
	case 0x4f: // DEC DI (DEC EDI)
		if (opsize  == size16) {
			DEC_R16(di);
		} else {
			DEC_R32(edi);
		}
		break;

#define JCCWD(STR, COND)					\
	if (opsize == size16) {					\
		DAS_prt_post_op(3);				\
		dst = mem->read16(get_seg_adr(CS, ++eip));	\
		DAS_pr(#STR" 0x%04x\n\n", dst);			\
		if (COND) {			   		\
			eip += (s16)(dst + 2);			\
		} else {					\
			eip += 2;				\
		}						\
	} else {						\
		DAS_prt_post_op(5);				\
		dst = mem->read32(get_seg_adr(CS, ++eip));	\
		DAS_pr(#STR" 0x%08x\n\n", dst);			\
		if (COND) {			   		\
			eip += (s16)(dst + 4);			\
		} else {					\
			eip += 4;				\
		}						\
	}

	case 0x0f:
		subop = mem->read8(get_seg_adr(CS, eip));
		switch (subop) {
		case 0x01: // LGDT/LIDT
			DAS_prt_post_op(2);
			dst = mem->read16(get_seg_adr(CS, ++eip));
			DAS_pr("%s ", (dst >> 3 & 7) == 2?"LGDT":"LIDT");
			DAS_modrm(dst, false, true, fword);
			if ((dst >> 3 & 7) == 2) { // LGDT
				tmpadr = modrm_seg_ea(dst);
				gdtr.limit = mem->read16(tmpadr);
				gdtr.base = (opsize == size16)? mem->read32(tmpadr + 2) & 0x00ffffff : mem->read32(tmpadr + 2);
			} else if ((dst >> 3 & 7) == 3) { // LIDT
				DAS_pr("xxxxx\n\n");
			}
			eip++;
			break;
		case 0x20: // MOV r32, CR0
			DAS_prt_post_op(2);
			modrm = mem->read8(get_seg_adr(CS, ++eip));
			DAS_pr("MOV ");
			DAS_modrm(modrm, false, false, dword);
			tmpb = modrm >> 3 & 7;
			DAS_pr("CR%d\n\n", tmpb);
			genregd(modrm & 7) = cr[tmpb];
			eip++;
			break;
		case 0x22: // MOV CR0, r32
			DAS_prt_post_op(2);
			modrm = mem->read8(get_seg_adr(CS, ++eip));
			tmpb = modrm >> 3 & 7;
			DAS_pr("MOV CR%d, ", tmpb);
			DAS_modrm(modrm, false, true, dword);
			cr[tmpb] = genregd(modrm & 7);
			if (tmpb == 0) {
				if (isRealMode && cr[0] & 1) {
					isRealMode = false;
					DAS_pr("RealMode -> ProtectedMode\n");
				} else if (!isRealMode && !(cr[0] & 1)) {
					isRealMode = true;
					// xxx リアルモードでeipを使うので
					// 上位ビットをクリアしておく
					// 実際はどうなるのだろう
					eip &= 0x0000ffff;
					DAS_pr("ProtectedMode -> RealMode\n");
				}
			}
			eip++;
			break;

		case 0x80: // JO rel16 (JO rel32)
			JCCWD(JO, flagu8 & OFSET8);
			break;
		case 0x81: // JNO rel16 (JNO rel32)
			JCCWD(JNO, !(flagu8 & OFSET8));
			break;
		case 0x82: // JB/JC/JNAE rel16 (JC/JNAE rel32)
			JCCWD(JB/JC/JNAE, flag8 & CF);
			break;
		case 0x83: // JNB/JNC/JAE rel16 (JNB/JNC rel32)
			JCCWD(JNB/JNC/JAE, !(flag8 & CF));
			break;
		case 0x84: // JE/JZ rel16 (JE/JZ rel32)
			JCCWD(JE/JZ, flag8 & ZF);
			break;
		case 0x85: // JNE/JNZ rel16 (JNE/JNZ rel32)
			JCCWD(JNE/JNZ, !(flag8 & ZF));
			break;
		case 0x86: // JBE/JNA rel16 (JBE/JNA rel32)
			JCCWD(JBE/JNA, flag8 & CF || flag8 & ZF);
			break;
		case 0x87: // JNBE/JA rel16 (JNBE/JA rel32)
			JCCWD(JNBE/JA, !(flag8 & CF) && !(flag8 & ZF));
			break;
		case 0x88: // JS rel16 (JS rel32)
			JCCWD(JS, flag8 & SF);
			break;
		case 0x89: // JNS rel16 (JNS rel32)
			JCCWD(JNS, !(flag8 & SF));
			break;
		case 0x8a: // JP/JPE rel16 (JP/JPE rel32)
			JCCWD(JP/JPE, flag8 & PF);
			break;
		case 0x8b: // JNP/JPO rel16 (JP/JPE rel32)
			JCCWD(JNP/JPO, !(flag8 & PF));
			break;
		case 0x8c: // JL/JNGE rel16 (JL/JNGE rel32)
			JCCWD(JL/JNGE, (flag8 ^ flagu8 << 4) & 0x80);
			break;
		case 0x8d: // JGE/JNL rel16 (JGE/JNL rel32)
			JCCWD(JGE/JNL, !((flag8 ^ flagu8 << 4) & 0x80));
			break;
		case 0x8e: // JLE/JNG rel8
			JCCWD(JLE/JNG, flag8 & ZF || (flag8 ^ flagu8 << 4) & 0x80);
			break;
		case 0x8f: // JNLE/JG rel8
			JCCWD(JNLE/JG, !(flag8 & ZF) && !((flag8 ^ flagu8 << 4) & 0x80));
		break;

		case 0xa0: // PUSH FS
			PUSH_SEG2(FS); // 2バイト命令用マクロ
			break;
		case 0xa1: // POP FS
			POP_SEG2(FS);
			break;
		case 0xa8: // PUSH GS
			PUSH_SEG2(GS);
			break;
		case 0xa9: // POP GS
			POP_SEG2(GS);
			break;
		case 0xb7: // MOVZX r32,r/m16
			modrm = mem->read8(get_seg_adr(CS, ++eip));
			DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
			DAS_pr("MOVZX ");
			// xxx 現状 MOVZX ESP, EAXの様にsrcが32bit表記になる
			DAS_modrm(modrm, true, true, dword);
			eip++;
			genregd(modrm >> 3 & 7) = modrmw(modrm);
			break;
		default:
			DAS_pr("xxxxx\n\n");
			// LFS/LGS/LSS... (80386)
		}
		break;

/******************** Jcc ********************/

#define JCC(STR, COND)				\
	DAS_prt_post_op(1);			\
	dst = mem->read8(get_seg_adr(CS, eip));	\
	DAS_pr(#STR" 0x%02x\n\n", dst);		\
	if (COND) {			   	\
		eip += (s8)dst + 1;		\
	} else {				\
		eip++;				\
	}

	case 0x70: // JO rel8
		JCC(JO, flagu8 & OFSET8);
		break;
	case 0x71: // JNO rel8
		JCC(JNO, !(flagu8 & OFSET8));
		break;
	case 0x72: // JB/JC/JNAE rel8
		JCC(JB/JC/JNAE, flag8 & CF);
		break;
	case 0x73: // JNB/JNC/JAE rel8
		JCC(JNB/JNC/JAE, !(flag8 & CF));
		break;
	case 0x74: // JE/JZ rel8
		JCC(JE/JZ, flag8 & ZF);
		break;
	case 0x75: // JNE/JNZ rel8
		JCC(JNE/JNZ, !(flag8 & ZF));
		break;
	case 0x76: // JBE/JNA rel8
		JCC(JBE/JNA, flag8 & CF && flag8 & ZF);
		break;
	case 0x77: // JNBE/JA rel8
		JCC(JNBE/JA, !(flag8 & CF) && !(flag8 & ZF));
		break;
	case 0x78: // JS rel8
		JCC(JS, flag8 & SF);
		break;
	case 0x79: // JNS rel8
		JCC(JNS, !(flag8 & SF));
		break;
	case 0x7a: // JP/JPE rel8
		JCC(JP/JPE, flag8 & PF);
		break;
	case 0x7b: // JNP/JPO rel8
		JCC(JNP/JPO, !(flag8 & PF));
		break;
	case 0x7c: // JL/JNGE rel8
		JCC(JL/JNGE, (flag8 ^ flagu8 << 4) & 0x80);
		break;
	case 0x7d: // JNL/JGE rel8
		JCC(JNL/JGE, !((flag8 ^ flagu8 << 4) & 0x80));
		break;
	case 0x7e: // JLE/JNG rel8
		JCC(JLE/JNG, flag8 & ZF || (flag8 ^ flagu8 << 4) & 0x80);
		break;
	case 0x7f: // JNLE/JG rel8
		JCC(JNLE/JG, !(flag8 & ZF) && !((flag8 ^ flagu8 << 4) & 0x80));
		break;

	case 0xe3:
		JCC(JCXZ, !cx);
		break;

/*
          76  543 210
+--------+-----------+---------+---------+--------+---------------+
|100000sw|mod ??? r/m|(DISP-LO)|(DISP-HI)|  data  |(data if sw=01)|
+--------+-----------+---------+---------+--------+---------------+
???(ここではregではなく、opの拡張。これにより以下の様に命令が変わる):
000:ADD, 001:OR, 010:ADC, 011:SBB, 100:AND, 101:SUB, 110:XOR, 111:CMP
 */
#define CAL_RM_IM(BWD, BWD2, OP, CAST, CRY, IPINC, ANDN)	\
	eip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 7);			\
		src = mem->read##BWD2(get_seg_adr(CS, eip));	\
		res = dst OP src + CRY;				\
		genreg##BWD(modrm & 7) = (CAST)res;		\
	} else {						\
		tmpadr = modrm_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		src = mem->read##BWD2(get_seg_adr(CS, eip));	\
		res = dst OP src + CRY;				\
		mem->write##BWD(tmpadr, (CAST)res);		\
	}						       	\
	eip += IPINC;						\
	flag8 = flag_cal##BWD[res ANDN];			\
	flag8 |= (dst ^ src ^ res) & AF;

#define LOGOP_RM_IM(BWD, BWD2, OP, IPINC)			\
	eip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 7);			\
		src = mem->read##BWD2(get_seg_adr(CS, eip));	\
		dst OP##= src;					\
		genreg##BWD(modrm & 7) = dst;			\
	} else {						\
		tmpadr = modrm_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		src = mem->read##BWD2(get_seg_adr(CS, eip));	\
		dst OP##= src;					\
		mem->write##BWD(tmpadr, dst);			\
	}							\
	eip += IPINC;						\
	FLAG_LOGOP##BWD(dst)

#define CMP_RM_IM(BWD, BWD2, IPINC, ANDN, ANDN2)		\
	eip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 7);			\
		src = mem->read##BWD2(get_seg_adr(CS, eip));	\
		res = dst - src;				\
	} else {						\
		tmpadr = modrm_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		src = mem->read##BWD2(get_seg_adr(CS, eip));	\
		res = dst - src;				\
	}							\
	eip += IPINC;						\
	flag8 = flag_cal##BWD[res ANDN];			\
	flag8 |= (dst ^ src ^ res) & AF;			\
	(dst ^ res) & (dst ^ src) & ANDN2?			\
		flagu8 |= OFSET8 : flagu8 &= ~OFSET8;

	case 0x80:
		// go through
	case 0x82:
		modrm = mem->read8(get_seg_adr(CS, eip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
		DAS_pr("%s ", str8x[subop]);
		DAS_modrm(modrm, false, false, byte);
		DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, eip + nr_disp_modrm(modrm) + 1)));

		switch (subop) {
		case 0: // ADD r/m8, imm8
			CAL_RM_IM(b, b, +, u8, 0, 1, );
			(src ^ res) & (dst ^ res) & 0x80?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		case 1: // OR r/m8, imm8
			LOGOP_RM_IM(b, b, |, 1);
			break;
		case 2: // ADC r/m8, imm8
			CAL_RM_IM(b, b, +, u8, (flag8 & CF), 1, );
			(src ^ res) & (dst ^ res) & 0x80?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		case 3: // SBB r/m8, imm8
			CAL_RM_IM(b, b, -, u8, -(flag8 & CF), 1, & 0x1ff);
			(dst ^ res) & (dst ^ src) & 0x80?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		case 4: // AND r/m8, imm8
			LOGOP_RM_IM(b, b, &, 1);
			break;
		case 5: // SUB r/m8, imm8
			CAL_RM_IM(b, b, -, u8, 0, 1, & 0x1ff);
			(dst ^ res) & (dst ^ src) & 0x80?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		case 6: // XOR r/m8, imm8
			LOGOP_RM_IM(b, b, ^, 1);
			break;
		case 7: // CMP r/m8, imm8
			CMP_RM_IM(b, b, 1, & 0x1ff, 0x80);
			break;
		}
		break;

	case 0x81:
		modrm = mem->read8(get_seg_adr(CS, eip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + (opsize == size16)?3:5);
		DAS_pr("%s ", str8x[subop]);
		DAS_modrm(modrm, false, false, word);
		DAS_pr((opsize == size16)?"0x%04x\n\n":"0x%08x\n\n", (opsize == size16)?mem->read16(get_seg_adr(CS, eip + 1)):mem->read32(get_seg_adr(CS, eip + 1)));

		switch (subop) {

		case 0: // ADD r/m16, imm16
			CAL_RM_IM(w, w, +, u16, 0, 2, );
			(dst ^ res) & (src ^ res) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		case 1: // OR r/m16, imm16
			LOGOP_RM_IM(w, w, |, 2);
			break;
		case 2: // ADC r/m16, imm16
			CAL_RM_IM(w, w, +, u16, (flag8 & CF), 2, );
			(dst ^ res) & (src ^ res) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		case 3: // SBB r/m16, imm16
			CAL_RM_IM(w, w, -, u16, -(flag8 & CF), 2, & 0x1ffff);
			(dst ^ res) & (dst ^ src) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		case 4: // AND r/m16, imm16 (AND r/m32, imm32)
			if (opsize == size16) {
				LOGOP_RM_IM(w, w, &, 2);
			} else {
				LOGOP_RM_IM(d, d, &, 4);
			}
			break;
		case 5: // SUB r/m16, imm16
			CAL_RM_IM(w, w, -, u16, 0, 2, & 0x1ffff);
			(dst ^ res) & (dst ^ src) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;

		case 6: // XOR r/m16, imm16
			LOGOP_RM_IM(w, w, ^, 2);
			break;
		case 7: // CMP r/m16, imm16
			CMP_RM_IM(w, w, 2, & 0x1ffff, 0x8000);
			break;
		}
		break;

	case 0x83: // ADD/ADC/AND/SUB/SBB/CMP r/m16, imm8 (... r/m32, imm8)
		//w-bit 1なのでワード動作、s-bit 0なので即値は byte
		modrm = mem->read8(get_seg_adr(CS, eip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
		DAS_pr("%s ", str8x[subop]);
		DAS_modrm(modrm, false, false, word);
		DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, eip + 1)));

		switch (subop) {
		case 0: // ADD r/m16, imm8
			CAL_RM_IM(w, b, +, u16, 0, 1, );
			(dst ^ res) & (src ^ res) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		case 1: // OR r/m16, imm8
			LOGOP_RM_IM(w, b, |, 1);
			break;
		case 2: // ADC r/m16, imm8
			CAL_RM_IM(w, b, +, u16, (flag8 & CF), 1, );
			(dst ^ res) & (src ^ res) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		case 3: // SBB r/m16, imm8
			CAL_RM_IM(w, b, -, u16, -(flag8 & CF), 1, & 0x1ffff);
			(dst ^ res) & (dst ^ src) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		case 4: // AND r/m16, imm8
			LOGOP_RM_IM(w, b, &, 1);
			break;
		case 5: // SUB r/m16, imm8
			CAL_RM_IM(w, b, -, u16, 0, 1, & 0x1ffff);
			(dst ^ res) & (dst ^ src) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		case 6: // XOR r/m16, imm8
			LOGOP_RM_IM(w, b, ^, 1);
			break;
		case 7: // CMP r/m16, imm8
			CMP_RM_IM(w, b, 1, & 0x1ffff, 0x8000);
			break;

		}
		break;

/******************** XCHG ********************/
/*
          76  543 210
+--------+-----------+---------+---------+
|1000011w|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
フラグは影響なし
 */

#define XCHG_R_RM(BWD)						\
	modrm = mem->read8(get_seg_adr(CS, eip));		\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);		\
	DAS_pr("XCHG ");					\
	DAS_modrm(modrm, true, true, BWD##word);		\
	greg = modrm >> 3 & 7;					\
	eip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		rm = modrm & 7;					\
		dst = genreg##BWD(rm);				\
		genreg##BWD(rm) = genreg##BWD(greg);		\
		genreg##BWD(greg) = dst;			\
	} else {						\
		tmpadr = modrm_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		mem->write##BWD(tmpadr, genreg##BWD(greg));	\
		genreg##BWD(greg) = dst;			\
	}

	case 0x86: // XCHG r8, r/m8 or XCHG r/m8, r8
		XCHG_R_RM(b);
		break;
	case 0x87: // XCHG r16, r/m16 or XCHG r/m16, r16
		XCHG_R_RM(w);
		break;

#define XCHG_GENREG(reg)		\
	DAS_prt_post_op(0);		\
	DAS_pr("XCHG AX, "#reg"\n\n");	\
	dst = ax;			\
	ax = reg;			\
	reg = dst;

	case 0x90: // XCHG AX
		XCHG_GENREG(ax);
		break;
	case 0x91: // XCHG CX
		XCHG_GENREG(cx);
		break;
	case 0x92: // XCHG DX
		XCHG_GENREG(dx);
		break;
	case 0x93: // XCHG BX
		XCHG_GENREG(bx);
		break;
	case 0x94: // XCHG SP
		XCHG_GENREG(sp);
		break;
	case 0x95: // XCHG BP
		XCHG_GENREG(bp);
		break;
	case 0x96: // XCHG SI
		XCHG_GENREG(si);
		break;
	case 0x97: // XCHG DI
		XCHG_GENREG(di);
		break;

/******************** MOV ********************/
/*
          76  543 210
+--------+-----------+---------+---------+
|100010dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
 */
	case 0x88: // MOV r/m8, r8
		modrm = mem->read8(get_seg_adr(CS, eip)); // modR/Mを読み込む
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm(modrm, true, false, byte);
		eip++;
		if ((modrm & 0xc0) == 0xc0) {
			genregb(modrm & 0x07) = genregb(modrm >> 3 & 7);
		} else {
			mem->write8(modrm_seg_ea(modrm), genregb(modrm >> 3 & 7));
		}
		break;

	case 0x89: // MOV r/m16, r16 (MOV r/m32, r32)
		modrm = mem->read8(get_seg_adr(CS, eip)); // modR/Mを読み込む
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm(modrm, true, false, word);
		eip++;
		if ((modrm & 0xc0) == 0xc0) {
			genregw(modrm & 0x07) = genregw(modrm >> 3 & 7);
		} else {
			mem->write16(modrm_seg_ea(modrm), genregw(modrm >> 3 & 7));
		}
		break;

	case 0x8a: // MOV r8, r/m8
		modrm = mem->read8(get_seg_adr(CS, eip)); // modR/Mを読み込む
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm(modrm, true, true, byte);
		eip++;
		genregb(modrm >> 3 & 7) = modrmb(modrm);
		break;

	case 0x8b: // MOV r16, r/m16 (MOV r32, r/m32)
		modrm = mem->read8(get_seg_adr(CS, eip)); // modR/Mを読み込む
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm(modrm, true, true, word);
		eip++;
		if (opsize == size16) {
			genregw(modrm >> 3 & 7) = modrmw(modrm);
		} else {
			genregd(modrm >> 3 & 7) = modrmd(modrm);
		}
		break;
/*
          76  543 210
+--------+-----------+---------+---------+
|10001100|mod 0SR r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
 */
/*
  参考文献(7)p3-491 本命令には32bitモードで動作時にOPサイズプリフィックスが
  つくことがあるが、気にしない。
*/
	case 0x8c: // MOV r/m16, Sreg
		modrm = mem->read8(get_seg_adr(CS, eip)); // modR/Mを読み込む
		sreg = modrm >> 3 & 3;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm(modrm, false, false, word);
		DAS_pr("%s\n\n", segreg_name[sreg]);
		eip++;
		if ((modrm & 0xc0) == 0xc0) {
			genregw(modrm & 0x07) = segreg[sreg];
		} else {
			mem->write16(modrm_seg_ea(modrm), segreg[sreg]);
		}
		break;

/*
          76  543 210
+--------+-----------+---------+---------+
|10001101|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
 */
	case 0x8d: // LEA r16, m (LEA r32, m)
		modrm = mem->read8(get_seg_adr(CS, eip)); // modR/Mを読み込む
		greg = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("LEA ");
		DAS_modrm(modrm, true, true, opsize == size16? word : dword);
		eip++;
		if (opsize == size16) {
			genregw(greg) = modrm16_ea(modrm);
		} else {
			genregd(greg) = modrm32_ea(modrm);
		}
		break;
/*
          76  543 210
+--------+-----------+---------+---------+
|10001110|mod 0SR r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
 */
	case 0x8e: // MOV Sreg, r/m16
		modrm = mem->read8(get_seg_adr(CS, eip)); // modR/Mを読み込む
		sreg = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV %s, ", segreg_name[sreg]);
		DAS_modrm(modrm, false, true, word);
		eip++;
		update_segreg(sreg, modrmw(modrm)); // セグメントはbaseも更新
		break;

/*
+---------+--------+--------+
|1010 000w|addr-lo |addr-hi |
+---------+--------+--------+
 */
	case 0xa0: // MOV AL, moffs8
		DAS_prt_post_op(2);
		src = mem->read16(get_seg_adr(CS, eip));
		DAS_pr("MOV AL, byte ptr [0x%04x]\n\n", src);
		al = mem->read8(get_seg_adr(DS, src));
		eip += 2;
		break;
	case 0xa1: // MOV AX, moffs16 (MOV EAX moffs32)
		if (opsize == size16) {
			DAS_prt_post_op(2);
			src = mem->read16(get_seg_adr(CS, eip));
			DAS_pr("MOV AX, word ptr [0x%04x]\n\n", src);
			ax = mem->read16(get_seg_adr(DS, src));
			eip += 2;
		} else {
			DAS_prt_post_op(4);
			src = mem->read32(get_seg_adr(CS, eip));
			DAS_pr("MOV EAX, word ptr [0x%08x]\n\n", src);
			eax = mem->read32(get_seg_adr(DS, src));
			eip += 4;
		}
		break;
	case 0xa2: // MOV moffs8, AL
		DAS_prt_post_op(2);
		src = mem->read16(get_seg_adr(CS, eip));
		DAS_pr("MOV AL, byte ptr [0x%04x]\n\n", src);
		mem->write8(get_seg_adr(DS, src), al);
		eip += 2;
		break;
	case 0xa3: // MOV moffs16, AX (MOV moffs32, EAX)
		if (opsize == size16) {
			DAS_prt_post_op(2);
			src = mem->read16(get_seg_adr(CS, eip));
			DAS_pr("MOV AX, word ptr [0x%04x]\n\n", src);
			mem->write16(get_seg_adr(DS, src), ax);
			eip += 2;
		} else {
			DAS_prt_post_op(4);
			src = mem->read32(get_seg_adr(CS, eip));
			DAS_pr("MOV EAX, word ptr [0x%08x]\n\n", src);
			mem->write32(get_seg_adr(DS, src), eax);
			eip += 4;
		}
		break;

/*
 76543 210
+---------+--------+-------------+
|1011w reg|  data  |(data if w=1)|
+---------+--------+-------------+
 */
	case 0xb0: // MOV AL, imm8
		// go through
	case 0xb1: // MOV CL, imm8
		// go through
	case 0xb2: // MOV DL, imm8
		// go through
	case 0xb3: // MOV BL, imm8
		// go through
	case 0xb4: // MOV AH, imm8
		// go through
	case 0xb5: // MOV CH, imm8
		// go through
	case 0xb6: // MOV DH, imm8
		// go through
	case 0xb7: // MOV BH, imm8
		DAS_prt_post_op(1);
		DAS_pr("MOV %s, 0x%02x\n\n", genreg_name[0][op & 7], mem->read8(get_seg_adr(CS, eip)));
		*genregb[op & 7] = mem->read8(get_seg_adr(CS, eip++));
		break;
	case 0xb8: // MOV AX, imm16
		// go through
	case 0xb9: // MOV CX, imm16
		// go through
	case 0xba: // MOV DX, imm16
		// go through
	case 0xbb: // MOV BX, imm16
		// go through
	case 0xbc: // MOV SP, imm16
		// go through
	case 0xbd: // MOV BP, imm16
		// go through
	case 0xbe: // MOV SI, imm16
		// go through
	case 0xbf: // MOV DI, imm16
		if (opsize == size16) {
			DAS_prt_post_op(2);
			DAS_pr("MOV %s, 0x%04x\n\n", genreg_name[1][op & 7], mem->read16(get_seg_adr(CS, eip)));
			genregw(op & 7) = mem->read16(get_seg_adr(CS, eip));
			eip += 2;
		} else {
			DAS_prt_post_op(4);
			DAS_pr("MOV %s, 0x%08x\n\n", genreg_name[2][op & 7], mem->read32(get_seg_adr(CS, eip)));
			genregd(op & 7) = mem->read32(get_seg_adr(CS, eip));
			eip += 4;
		}
		break;

/*
          76  543 210
+--------+-----------+---------+---------+--------+-------------+
|1100011w|mod 000 r/m|(DISP-LO)|(DISP-HI)|  data  |(data if w=1)|
+--------+-----------+---------+---------+--------+-------------+
 */
	case 0xc6: // MOV r/m8, imm8
		modrm = mem->read8(get_seg_adr(CS, eip));
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm(modrm, false, false, byte);
		DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, eip + nr_disp_modrm(modrm) + 1)));
		eip++;
		if ((modrm & 0xc0) == 0xc0) {
			genregb(modrm & 7) = mem->read8(get_seg_adr(CS, eip));
		} else {
			mem->write8(modrm_seg_ea(modrm), mem->read8(get_seg_adr(CS, eip)));
			eip++;
		}
		break;
	case 0xc7: // MOV r/m16, imm16
		modrm = mem->read8(get_seg_adr(CS, eip));
		DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
		DAS_pr("MOV ");
		DAS_modrm(modrm, false, false, word);
		DAS_pr("0x%04x\n\n", mem->read16(get_seg_adr(CS, eip + nr_disp_modrm(modrm) + 1)));
		eip++;
		if ((modrm & 0xc0) == 0xc0) {
			genregw(modrm & 7) = mem->read16(get_seg_adr(CS, eip));
		} else {
			mem->write16(modrm_seg_ea(modrm), mem->read16(get_seg_adr(CS, eip)));
			eip += 2;
		}
		break;

/******************** TEST ********************/
// OF/CF:0, SF/ZF/PF:結果による, AF:未定義

#define TEST_RM_R(BWD)						\
	modrm = mem->read8(get_seg_adr(CS, eip));		\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);		\
	DAS_pr("TEST ");					\
	DAS_modrm(modrm, true, false, BWD##word);		\
	eip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 0x07)			\
			& genreg##BWD(modrm >> 3 & 7);		\
	} else {						\
		dst = mem->read##BWD(modrm_seg_ea(modrm))	\
			& genreg##BWD(modrm >> 3 & 7);		\
	}							\
	flag8 = flag_cal##BWD[dst];				\
	flagu8 &= ~OFSET8;

	case 0x84: // TEST r/m8, r8
		TEST_RM_R(b);
		break;
	case 0x85: // TEST r/m16, r16 (TEST rm/32, r32)
		TEST_RM_R(w);
		break;

	case 0xa8: // test al, imm8
		DAS_prt_post_op(1);
		DAS_pr("TEST AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, eip)));
		dst = al & mem->read8(get_seg_adr(CS, eip++));
		flag8 = flag_calb[dst];
		flagu8 &= ~OFSET8;
		break;
	case 0xa9: // test ax, imm16 (test eax, imm32)
		DAS_prt_post_op(2);
		DAS_pr("TEST AX, 0x%04x\n\n", mem->read16(get_seg_adr(CS, eip)));
		dst = ax & mem->read16(get_seg_adr(CS, eip));
		eip += 2;
		flag8 = flag_calw[dst];
		flagu8 &= ~OFSET8;
		break;

/******************** CBW/CWD/CDQ ********************/

	case 0x98: // CBW
		DAS_prt_post_op(0);
		DAS_pr("CBW\n\n");
		ah = (al & 0x80)? 0xff : 0x0 ;
		break;
	case 0x99: // CWD (CDQ)
		DAS_prt_post_op(0);
		DAS_pr("CWD\n\n");
		if (ax & 0x8000) {
			dx = 0xffff;
		} else {
			dx = 0;
		}
		break;

/******************** WAIT ********************/

	case 0x9b:
		DAS_prt_post_op(0);
		DAS_pr("WAIT\n\n");
		// コプロ未実装なのでなにもしない
		break;

/******************** SAHF/LAHF ********************/

	case 0x9e:
		DAS_prt_post_op(0);
		DAS_pr("SAHF\n\n");
		flag8 = 0x2; // xxx eflagsのbit 1は常に1らしい
		flag8 |= ah;
		break;
	case 0x9f:
		DAS_prt_post_op(0);
		DAS_pr("LAHF\n\n");
		ah = flag8;
		break;


/******************** MOVS ********************/

	case 0xa4: // MOVS m8, m8
		DAS_prt_post_op(0);
		DAS_pr("MOVSB\n\n");
	        (repe_prefix)? cnt = cx, cx = 0 : cnt = 1;
		while (cnt != 0) {
			mem->write8(get_seg_adr(ES, di),
				    mem->read8(get_seg_adr(DS, si)));
			di++;
			si++;
			cnt--;
		}
		break;
	case 0xa5: // MOVS m16, m16 (MOVS m32, m32)
		DAS_prt_post_op(0);
		DAS_pr("MOVSW\n\n");
	        (repe_prefix)? cnt = cx, cx = 0 : cnt = 1;
		while (cnt != 0) {
			mem->write16(get_seg_adr(ES, di),
				     mem->read16(get_seg_adr(DS, si)));
			di += 2;
			si += 2;
			cnt--;
		}
		break;

/******************** CMPS ********************/

	case 0xa6:
		DAS_prt_post_op(0);
		DAS_pr("CMPSB\n\n");
		(repne_prefix)?	cnt = cx : cnt = 1;
		incdec = (flagu8 & DF8)? -1 : +1;
		while (cnt != 0) {
			dst = mem->read8(get_seg_adr(DS, si));
			src = mem->read8(get_seg_adr(ES, di));
			res = dst - src;
			if (res == 0) { // ZF==1
				break;
			}
			si += incdec;
			di += incdec;
			cnt--;
		}
		cx = cnt;
		flag8 = flag_calb[res & 0x1ff];
		flag8 |= (dst ^ src ^ res) & AF;
		(dst ^ res) & (dst ^ src) & 0x80?
			flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
		break;
	case 0xa7: // CMPSW (CMPSD)
		DAS_prt_post_op(0);
		DAS_pr("CMPSW\n\n");
		(repne_prefix)?	cnt = cx : cnt = 1;
		incdec = (flagu8 & DF8)? -2 : +2;
		while (cnt != 0) {
			dst = mem->read16(get_seg_adr(DS, si));
			src = mem->read16(get_seg_adr(ES, di));
			res = dst - src;
			if (res == 0) { // ZF==1
				break;
			}
			si += incdec;
			di += incdec;
			cnt--;
		}
		cx = cnt;
		flag8 = flag_calb[res & 0x1ffff];
		flag8 |= (dst ^ src ^ res) & AF;
		(dst ^ res) & (dst ^ src) & 0x8000?
			flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
		break;

/******************** STOS ********************/

	case 0xaa:
		DAS_prt_post_op(0);
		DAS_pr("STOSB\n\n");
	        (repe_prefix)? cnt = cx, cx = 0 : cnt = 1;
		// whileの中で毎回ifするのは無駄なのであらかじめ加減値を算出する
		incdec = (flagu8 & DF8)? -1 : +1;
		while (cnt != 0) {
			mem->write8(get_seg_adr(ES, di), al);
			di += incdec;
			cnt--;
		}
		break;
	case 0xab:
		DAS_prt_post_op(0);
		if (opsize == size16) {
			DAS_pr("STOSW\n\n");
			(repe_prefix)? cnt = cx, cx = 0 : cnt = 1;
			incdec = (flagu8 & DF8)? -2 : +2;
			while (cnt != 0) {
				mem->write16(get_seg_adr(ES, di), ax);
				di += incdec;
				cnt--;
			}
		} else {
			DAS_pr("STOSD\n\n");
			(repe_prefix)? cnt = ecx, ecx = 0 : cnt = 1;
			incdec = (flagu8 & DF8)? -4 : +4;
			while (cnt != 0) {
				mem->write32(get_seg_adr(ES, edi), eax);
				edi += incdec;
				cnt--;
			}

		}
		break;

/******************** LODS ********************/

	case 0xac:
		DAS_prt_post_op(0);
		DAS_pr("LODSB\n\n");
	        (repe_prefix)? cnt = cx, cx = 0 : cnt = 1;
		while (cnt != 0) {
			al = mem->read8(get_seg_adr(DS, si));
			si++;
			cnt--;
		}
		break;
	case 0xad:
		DAS_prt_post_op(0);
		DAS_pr("LODSW\n\n");
	        (repe_prefix)? cnt = cx, cx = 0 : cnt = 1;
		while (cnt != 0) {
			// xxx プロテクトモードでオペレーションサイズ
			// オーバーライドした時だけ上位16bitも参照する?
			// それともリアルモードでも参照する?
			ax = mem->read16(get_seg_adr(DS, esi));
			si +=2;
			cnt--;
		}
		break;

/******************** SCAS ********************/

	case 0xae:
		DAS_prt_post_op(0);
		DAS_pr("SCASB\n\n");
		(repe_prefix || repne_prefix)? cnt = cx : cnt = 1;
		incdec = (flagu8 & DF8)? -1 : +1;
		while (cnt != 0) {
			dst = al;
			src = mem->read8(get_seg_adr(ES, di));
			res = dst - src;
			if (repne_prefix && res == 0) { // ZF==1
				break;
			}
			if (repe_prefix && res != 0) { // ZF==0
				break;
			}
			di += incdec;
			cnt--;
		}
		cx = cnt;
		flag8 = flag_calb[res & 0x1ff];
		flag8 |= (dst ^ src ^ res) & AF;
		(dst ^ res) & (dst ^ src) & 0x80?
			flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
		break;
	case 0xaf: // SCASW (SCASD)
		DAS_prt_post_op(0);
		if (opsize == size16) {
			DAS_pr("SCASW\n\n");
			(repe_prefix || repne_prefix)? cnt = cx : cnt = 1;
			incdec = (flagu8 & DF8)? -2 : +2;
			while (cnt != 0) {
				dst = ax;
				src = mem->read16(get_seg_adr(ES, di));
				res = dst - src;
				if (repne_prefix && res == 0) { // ZF==1
					break;
				}
				if (repe_prefix && res != 0) { // ZF==0
					break;
				}
				di += incdec;
				cnt--;
			}
			cx = cnt;
			flag8 = flag_calb[res & 0x1ffff];
			flag8 |= (dst ^ src ^ res) & AF;
			(dst ^ res) & (dst ^ src) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
		} else {
			DAS_pr("SCASD\n\n");
			(repe_prefix || repne_prefix)? cnt = ecx : cnt = 1;
			incdec = (flagu8 & DF8)? -4 : +4;
			while (cnt != 0) {
				dst = eax;
				src = mem->read32(get_seg_adr(ES, edi));
				res = dst - src;
				if (repne_prefix && res == 0) { // ZF==1
					break;
				}
				if (repe_prefix && res != 0) { // ZF==0
					break;
				}
				edi += incdec;
				cnt--;
			}
			ecx = cnt;
			/* xxx flag */
		}
		break;

/******************** Rotate/Shift ********************/
/*
          76  543 210
+--------+-----------+---------+---------+--------+
|11000000|mod op2 r/m|(DISP-LO)|(DISP-HI)|  data  |
+--------+-----------+---------+---------+--------+
 */
	case 0xc0: // 80386
		modrm = mem->read8(get_seg_adr(CS, eip));
		subop = modrm >> 3 & 7;
		ndisp = nr_disp_modrm(modrm);
		DAS_prt_post_op(ndisp + 2);
		DAS_pr("%s ", strdx[subop]);
		DAS_modrm(modrm, false, false, byte);
		DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, eip + ndisp + 1)));
		switch (subop) {
		// C0 /5 ib
		case 0x5: // SHR r/m8, imm8
			eip++;
			src = mem->read8(get_seg_adr(CS, ndisp + eip));
			if ((modrm & 0xc0) == 0xc0) {
				dst = genregb(modrm & 0x07);
				genregb(modrm & 0x07) = dst >> src;
			} else {
				tmpadr = modrm_seg_ea(modrm);
				dst = mem->read8(tmpadr);
				mem->write8(tmpadr, dst >> src);
			}
			eip++;
			if (src != 0) { // xxx フラグは要再確認
				flag8 = flag_calb[dst >> src];
				flag8 |= AF; // NP2/NP21に合わせる
				// 元の値の7bitと6bitを比較する
				(src ^ src >> 1) & 0x40?
					flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			}
			break;
		default:
			DAS_pr("xxxxx\n\n");
		}
		break;


/*
          76  543 210
+--------+-----------+---------+---------+--------+
|11000001|mod op2 r/m|(DISP-LO)|(DISP-HI)|  data  |
+--------+-----------+---------+---------+--------+
 */
	case 0xc1: // 80386
		modrm = mem->read8(get_seg_adr(CS, eip));
		subop = modrm >> 3 & 7;
		ndisp = nr_disp_modrm(modrm);
		DAS_prt_post_op(ndisp + 2);
		DAS_pr("%s ", strdx[subop]);
		DAS_modrm(modrm, false, false, word);
		DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, eip + ndisp + 1)));
		switch (subop) {
		// C1 /4 ib
		case 0x4: // SAL/SHL r/m16, imm8 (SAL/SHL r/m32, imm8)
			eip++;
			src = mem->read8(get_seg_adr(CS, ndisp + eip));
			if (opsize == size16) {
				if ((modrm & 0xc0) == 0xc0) {
					dst = genregw(modrm & 0x07);
					genregw(modrm & 0x07) = dst << src;
				} else {
					tmpadr = modrm_seg_ea(modrm);
					dst = mem->read16(tmpadr);
					mem->write16(tmpadr, dst << src);
				}
				eip++;
				flag8 = flag_calw[dst << src & 0x1ffff];
				flag8 |= AF; // NP2/NP21に合わせる
				// 元の値の15bitと14bitを比較する
				(dst ^ dst >> 1) & 0x4000?
					flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			} else {
				if ((modrm & 0xc0) == 0xc0) {
					dst = genregd(modrm & 0x07);
					genregd(modrm & 0x07) = dst << src;
				} else {
					tmpadr = modrm_seg_ea(modrm);
					dst = mem->read32(tmpadr);
					mem->write32(tmpadr, dst << src);
				}
				eip++;
				// xxx flag
			}
			break;
		// C1 /5 ib
		case 0x5: // SHR r/m16, imm8
			eip++;
			src = mem->read8(get_seg_adr(CS, ndisp + eip));
			if ((modrm & 0xc0) == 0xc0) {
				dst = genregw(modrm & 0x07);
				genregw(modrm & 0x07) = dst >> src;
			} else {
				tmpadr = modrm_seg_ea(modrm);
				dst = mem->read16(tmpadr);
				mem->write16(tmpadr, dst >> src);
			}
			eip++;
			if (src != 0) { // xxx フラグは要再確認
				flag8 = flag_calw[dst >> src];
				flag8 |= AF; // NP2/NP21に合わせる
				// 元の値の15bitと14bitを比較する
				(dst ^ dst >> 1) & 0x4000?
					flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			}
			break;
		default:
			DAS_pr("xxxxx\n\n");
		}
		break;

#define ROT_L <<
#define ROT_R >>
#define ROT_ANDLb 0x80
#define ROT_ANDRb 0x01
#define ROT_ANDLw 0x8000
#define ROT_ANDRw 0x0001
#define ROT_AND2b 0x40
#define ROT_AND2w 0x4000

#define ROT_RM(BWD, DIR, SRC, DST, FUNC)			\
	eip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 0x07);		\
		FUNC;						\
		genreg##BWD(modrm & 0x07) = (BWD##CAST)res;	\
	} else {						\
		tmpadr = modrm_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		FUNC;						\
		mem->write##BWD(tmpadr, (BWD##CAST)res);	\
	}							\
	dst ROT_##DIR (SRC - 1) & ROT_AND##DIR##BWD?		\
		flag8 |= CF : flag8 &= ~CF;			\
	/* OFは1シフトの時影響し、その他の場合は		\
	   未定義だが常に計算する。0シフトは不変。		\
	   CF ^ MSB(DEST) or MSB(DEST) ^ MSB-1(DEST) */		\
	(DST ^ DST >> 1) & ROT_AND2##BWD?			\
		flagu8 |= OFSET8 : flagu8 &= ~OFSET8;

#define SFT_ANDb 0x100
#define SFT_ANDw 0x10000
#define SFT_AND2b 0x80
#define SFT_AND2w 0x8000
#define SFT_SALSHL(BWD, CNT, ANDN)				\
	eip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		res = genreg##BWD(modrm & 0x07) << CNT;		\
		genreg##BWD(modrm & 0x07) = (BWD##CAST)res;	\
	} else {						\
		tmpadr = modrm_seg_ea(modrm);			\
		res = mem->read##BWD(tmpadr) << CNT;		\
		mem->write##BWD(tmpadr, (BWD##CAST)res);	\
	}							\
	if (CNT != 0) {						\
		flag8 = flag_cal##BWD[res ANDN];		\
		flag8 |= AF; /* NP2/NP21に合わせる */		\
		/* OFは1シフトの時影響し、その他の場合は	\
		   未定義だが常に計算する。0シフトは不変。	\
		  CF ^ MSB(DEST)			*/	\
		(res ^ res << 1) & SFT_AND##BWD?		\
			flagu8 |= OFSET8 : flagu8 &= ~OFSET8;	\
	}

#define SFT_SHR(BWD, CNT)					\
	eip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 0x07);		\
		res = dst >> CNT;				\
		genregb(modrm & 0x07) = (BWD##CAST)res;		\
	} else {						\
		tmpadr = modrm_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		res = dst >> CNT;				\
		mem->write##BWD(tmpadr, (BWD##CAST)res);	\
	}							\
	if (CNT != 0) {						\
		flag8 = flag_cal##BWD[res]; /* CFオフ */	\
		flag8 |= dst >> (CNT - 1) & 1; /* CF */		\
		flag8 |= AF; /* NP2/NP21に合わせる */		\
		/* OFは1シフトの時影響し、その他の場合は	\
		   未定義だが常に計算する。0シフトは不変。	\
		   MSB(tempDEST)			*/	\
		(dst & SFT_AND2##BWD)?				\
			flagu8 |= OFSET8 : flagu8 &= ~OFSET8;	\
	}

#define SFT_SAR(BWD, CNT)					\
	eip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 7);			\
		res = dst >> CNT;				\
		if (dst & SFT_AND2##BWD) {			\
			res |= sar_bit##BWD[CNT];		\
		}						\
		genreg##BWD(modrm & 7) = (BWD##CAST)res;	\
	} else {						\
		tmpadr = modrm_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		res = dst >> CNT;				\
		if (dst & SFT_AND2##BWD) {			\
			res |= sar_bit##BWD[CNT];		\
		}						\
		mem->write##BWD(tmpadr, (BWD##CAST)res);	\
	}							\
	if (CNT != 0) {						\
		flag8 = flag_calb[res]; /* CFオフ */		\
		flag8 |= dst >> (CNT - 1) & 1; /* CF */		\
		flag8 |= AF; /* NP2/NP21に合わせる */		\
		/* OFは1シフトの時影響し、その他の場合は	\
		   未定義だが常に計算する。0シフトは不変。	\
		   常に0				*/	\
		flagu8 &= ~OFSET8;				\
	}

/*
          76  543 210
+--------+-----------+---------+---------+
|110100vw|mod op2 r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
 */
	case 0xd0:
		modrm = mem->read8(get_seg_adr(CS, eip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strdx[subop]);
		DAS_modrm(modrm, false, true, word);
		switch (subop) {
		// D0 /4 r/m8
		case 0x0: // ROL r/m8
			ROT_RM(b, L, 1, dst, res = dst >> 7 | dst << 1);
			break;
		case 0x1: // ROR r/m8
			ROT_RM(b, R, 1, res, res = dst << 7 | dst >> 1);
			break;
		case 0x2: // RCL r/m8
			ROT_RM(b, L, 1, dst, res = dst << 1 | (flag8 & CF));
			break;
		case 0x3: // RCR r/m8
			ROT_RM(b, R, 1, dst, res = dst >> 1 | (flag8 & CF) << 7);
			break;
		case 0x4: // SAL/SHL r/m8
			// go through
		case 0x6:
			SFT_SALSHL(b, 1, & 0x1ff);
			break;
		case 0x5: // SHR r/m8
			SFT_SHR(b, 1);
			break;
		case 0x7: // SAR r/m8
			SFT_SAR(b, 1);
			break;
		}
		break;

	case 0xd1:
		modrm = mem->read8(get_seg_adr(CS, eip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strdx[subop]);
		DAS_modrm(modrm, false, true, word);
		switch (subop) {
		// D1 /4 r/m16
		case 0x0: // ROL r/m16
			ROT_RM(w, L, 1, dst, res = dst >> 15 | dst << 1);
			break;
		case 0x1: // ROR r/m16
			ROT_RM(w, R, 1, res, res = dst << 15 | dst >> 1);
			break;
		case 0x2: // RCL r/m16
			ROT_RM(w, L, 1, dst, res = dst << 1 | (flag8 & CF));
			break;
		case 0x3: // RCR r/m16
			ROT_RM(w, R, 1, dst, res = dst >> 1 | (flag8 & CF) << 15);
			break;
		case 0x4: // SAL/SHL r/m16 (SAL/SHL r/m32)
			// go through
		case 0x6:
			SFT_SALSHL(w, 1, & 0x1ffff);
			break;
		case 0x5: // SHR r/m16
			SFT_SHR(w, 1);
			break;
		case 0x7: // SAR r/m16
			SFT_SAR(w, 1);
			break;
		}
		break;

	case 0xd2:
		modrm = mem->read8(get_seg_adr(CS, eip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strdx[subop]);
		DAS_modrm(modrm, false, false, byte);
		DAS_pr("CL\n\n");
		switch (subop) {
		case 0x0: // ROL r/m8, CL
			src = cl % 8;
			if (src != 0) {
				ROT_RM(b, L, src, dst,
					 res = dst >> (8 - src) | dst << src);
			}
			break;
		case 0x1: // ROR r/m8, CL
			src = cl % 8;
			if (src != 0) {
				ROT_RM(b, R, src, res,
					 res = dst << (8 - src) | dst >> src);
			}
			break;
		case 0x2: // RCL r/m8, CL
			src = cl % 8;
			if (src != 0) {
				ROT_RM(b, L, src, dst,
					 res = dst << src | (flag8 & CF) << (src - 1) | dst >> (8 - src + 1));
			}
			break;
		case 0x3: // RCR r/m8, CL
			src = cl % 8;
			if (src != 0) {
				ROT_RM(b, R, src, dst,
					 res = dst >> src | (flag8 & CF) << (8 - src) | dst << (8 - src + 1));
			}
			break;
		case 0x4: // SAL/SHL r/m8, CL
			// go through
		case 0x6:
			SFT_SALSHL(b, cl, & 0x1ff);
			break;
		case 0x5: // SHR r/m8, CL
			SFT_SHR(b, cl);
			break;
		case 0x7: // SAR r/m8, CL
			SFT_SAR(b, cl);
			break;
		}
		break;

	case 0xd3:
		modrm = mem->read8(get_seg_adr(CS, eip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strdx[subop]);
		DAS_modrm(modrm, false, false, word);
		DAS_pr("CL\n\n");
		switch (subop) {
		case 0x0: // ROL r/m16, CL
			src = cl % 16;
			if (src != 0) {
				ROT_RM(w, L, src, dst,
					 res = dst >> (16 - src) | dst << src);
			}
			break;
		case 0x1: // ROR r/m16, CL
			src = cl % 16;
			if (src != 0) {
				ROT_RM(w, R, src, res,
					 res = dst << (16 - src) | dst >> src);
			}
			break;
		case 0x2: // RCL r/m16, CL
			src = cl % 16;
			if (src != 0) {
				ROT_RM(w, L, src, dst,
					 res = dst << src | (flag8 & CF) << (src - 1) | dst >> (16 - src + 1));
			}
			break;
		case 0x3: // RCR r/m16, CL
			src = cl % 16;
			if (src != 0) {
				ROT_RM(w, R, src, dst,
					 res = dst >> src | (flag8 & CF) << (16 - src) | dst << (16 - src + 1));
			}
			break;
		// D3 /4 r/m16
		case 0x4: // SAL/SHL r/m16, CL (SAL/SHL r/m32, CL)
			// go through
		case 0x6:
			SFT_SALSHL(w, cl, & 0x1ffff);
			break;
		case 0x5: // SHR r/m16, CL
			SFT_SHR(w, cl);
			break;
		case 0x7: // SAR r/m16, CL
			SFT_SAR(w, cl);
			break;
		}
		break;

/******************** RET ********************/

	// xxx プロテクトモードでcall/retに対して
	// オペレーションサイズプリフィックスが使われることはないよね？
	case 0xc3: // RET  nearリターンする
		DAS_prt_post_op(0);
		DAS_pr("RET\n\n");
		POPW(ip);
		break;
	case 0xcb: // RET  farリターンする
		DAS_prt_post_op(0);
		DAS_pr("RET\n\n");
		POPW(ip);
		POPW(dst);
		update_segreg(CS, (u16)dst);
		break;
	case 0xc2: // RET  nearリターンする
		DAS_prt_post_op(1);
		// eipは後で書き換わるのであらかじめ取得しておく
		src = mem->read16(get_seg_adr(CS, eip));
		DAS_pr("RET 0x%04x\n\n", src);
		POPW(ip);
		sp += src;
		break;
	case 0xca: // RET  farリターンする
		DAS_prt_post_op(1);
		src = mem->read16(get_seg_adr(CS, eip));
		DAS_pr("RET 0x%04x\n\n", src);
		POPW(ip);
		POPW(dst);
		update_segreg(CS, (u16)dst);
		sp += src;
		break;

/******************** LES/LDS ********************/

#define LxS(STR, seg)							\
	modrm = mem->read8(get_seg_adr(CS, eip));			\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);			\
	DAS_pr(#STR" ");						\
	eip++;								\
	DAS_modrm(modrm, true, true, word);				\
	if ((modrm & 0xc0) == 0xc0) {					\
		/* xxx ソフトウェア例外らしい */			\
	} else {/* xxx NP2に合わせたが、genregとESの順番が逆かも */	\
		tmpadr = modrm_seg_ea(modrm);				\
		genregw(modrm >> 3 & 7) = mem->read16(tmpadr);		\
		update_segreg(seg, mem->read16(tmpadr + 2));		\
	}

	case 0xc4: // LES r16, m16:16 (LES r32, m16:32)
		LxS(LES, ES);
		break;
	case 0xc5: // LDS r16, m16:16 (LDS r32, m16:32)
		LxS(LDS, DS);
		break;

/******************** INT ********************/

	case 0xcc: // INT 3
		DAS_prt_post_op(0);
		DAS_pr("INT 3\n\n");
		PUSHW0(flagu8 << 8 | flag8);
		PUSHW0(segreg[CS]);
		PUSHW0(ip);
		flagu8 &= ~(TFSET8 | IFSET8);
		tmpadr = 3 * 4;
		eip = mem->read16(tmpadr);
		update_segreg(CS, mem->read16(tmpadr + 2));
		break;
	case 0xcd: // INT n
		DAS_prt_post_op(1);
		tmpb = mem->read8(get_seg_adr(CS, eip));
		DAS_pr("INT %d\n\n", tmpb);
		eip++;
		PUSHW0(flagu8 << 8 | flag8);
		PUSHW0(segreg[CS]);
		PUSHW0(ip);
		flagu8 &= ~(TFSET8 | IFSET8);
		tmpadr = tmpb * 4;
		eip = mem->read16(tmpadr);
		update_segreg(CS, mem->read16(tmpadr + 2));
		break;
	case 0xce: // INTO
		DAS_prt_post_op(0);
		DAS_pr("INTO\n\n");
		PUSHW0(flagu8 << 8 | flag8);
		PUSHW0(segreg[CS]);
		PUSHW0(ip);
		flagu8 &= ~(TFSET8 | IFSET8);
		eip = mem->read16(0x10);
		update_segreg(CS, mem->read16(0x12));
		break;
	case 0xcf: // IRET
		POPW0(ip);
		POPW0(warg1);
		update_segreg(CS, warg1);
		POPW0(warg1);
		flag8 = warg1 & 0xff;
		flagu8 = warg1 >> 8;
		break;

/******************** XLAT ********************/

	case 0xd7:
		DAS_prt_post_op(0);
		DAS_pr("XLAT\n\n");
		al = mem->read8(get_seg_adr(DS, (bx + al) & 0xffff));
		break;

/******************** ESC ********************/

	case 0xd8:
		// go through
	case 0xd9:
		// go through
	case 0xda:
		// nothing to do
		break;
	case 0xdb: // ESC 3
		DAS_prt_post_op(1);
	        subop = mem->read8(get_seg_adr(CS, eip));
		switch (subop) {
		case 0xe3: // FNINIT
			DAS_pr("FNINIT\n\n");
			// nothing to do
			eip++;
			break;
		}
		break;
	case 0xdc:
		// go through
	case 0xdd:
		// go through
	case 0xde:
		// go through
	case 0xdf:
		// nothing to do
		break;

/******************** LOOP ********************/

	case 0xe0: // LOOPNE/LOOPNZ rel8
		DAS_prt_post_op(1);
		tmpb = mem->read8(get_seg_adr(CS, eip));
		DAS_pr("LOOPNE/LOOPNZ 0x%02x\n\n", tmpb);
		eip++; // jmpする前にインクリメントしておく必要がある
		cx--;
		if (cx != 0 && !(flag8 & ZF)) {
			eip += (s8)tmpb;
		}
		break;
	case 0xe1: // LOOPE/LOOPZ rel8
		DAS_prt_post_op(1);
		tmpb = mem->read8(get_seg_adr(CS, eip));
		DAS_pr("LOOPE/LOOPZ 0x%02x\n\n", tmpb);
		eip++;
		cx--;
		if (cx != 0 && flag8 & ZF) {
			eip += (s8)tmpb;
		}
		break;
	case 0xe2: // LOOP rel8
		DAS_prt_post_op(1);
		tmpb = mem->read8(get_seg_adr(CS, eip));
		DAS_pr("LOOP 0x%02x\n\n", tmpb);
		eip++;
		cx--;
		if (cx != 0) {
			eip += (s8)tmpb;
		}
		break;

/******************** IN/OUT ********************/
/*
+--------+--------+
|1110010w| data-8 |
+--------+--------+
 */
	case 0xe4: // IN AL, imm8
		DAS_prt_post_op(1);
		DAS_pr("IN AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, eip)));
		al = io->read8(mem->read8(get_seg_adr(CS, eip++)));
		break;
	case 0xe5: // IN AX, imm8 (xxx IN EAX, imm8)
		DAS_prt_post_op(1);
		DAS_pr("IN AX, 0x%02x\n\n", mem->read8(get_seg_adr(CS, eip)));
		ax = io->read16(mem->read8(get_seg_adr(CS, eip++)));
		break;

/*
+--------+--------+
|1110011w| data-8 |
+--------+--------+
 */
	case 0xe6: // OUT imm8, AL
		DAS_prt_post_op(1);
		DAS_pr("OUT 0x%02x, AL\n\n", mem->read8(get_seg_adr(CS, eip)));
		io->write8(mem->read8(get_seg_adr(CS, eip++)), al);
		break;
	case 0xe7: // OUT imm8, AX
		DAS_prt_post_op(1);
		DAS_pr("OUT 0x%02x, AX\n\n", mem->read8(get_seg_adr(CS, eip)));
		io->write16(mem->read8(get_seg_adr(CS, eip++)), ax);
		break;

/*
+--------+
|1110110w|
+--------+
 */
	case 0xec: // IN AL, DX
		DAS_prt_post_op(0);
		DAS_pr("IN AL, DX\n\n");
		al = io->read8(dx);
		break;
	case 0xed: // IN AX, DX (xxx IN EAX, DX)
		DAS_prt_post_op(0);
		DAS_pr("IN AX, DX\n\n");
		ax = io->read16(dx);
		break;

/*
+--------+
|1110111w|
+--------+
 */
	case 0xee: // OUT DX, AL
		DAS_prt_post_op(0);
		DAS_pr("OUT DX, AL\n\n");
		io->write8(dx, al);
		break;
	case 0xef: // OUT DX, AX
		DAS_prt_post_op(0);
		DAS_pr("OUT DX, AX\n\n");
		io->write16(dx, ax);
		break;

/******************** CALL ********************/

	case 0xe8: // CALL rel16
		DAS_prt_post_op(2);
		warg1 = mem->read16(get_seg_adr(CS, eip));
		eip += 2;
		DAS_pr("CALL 0x%04x\n\n", warg1);
		PUSHW0(ip);
		eip += (s16)warg1;
		break;
/*
+--------+--------+--------+--------+--------+
|10011010| IP-lo  | IP-hi  | CS-lo  | CS-hi  |
+--------+--------+--------+--------+--------+
 */
	case 0x9a: // CALL ptr16:16 セグメント外直接
		DAS_prt_post_op(4);
		warg1 = mem->read16(get_seg_adr(CS, eip));
		warg2 = mem->read16(get_seg_adr(CS, eip + 2));
		eip += 4;
		DAS_pr("CALL %04x:%04x\n\n", warg2, warg1);
		PUSHW0(segreg[CS]);
		PUSHW0(ip);
		update_segreg(CS, warg2);
		eip = warg1;
		break;

/******************** JMP ********************/
/*
+--------+---------+---------+
|11101001|IP-INC-LO|IP-INC-HI|
+--------+---------+---------+
 */
	case 0xe9: // JMP rel16 (JMP rel32) セグメント内直接ジャンプ
		if (opsize == size16) {
			DAS_prt_post_op(2);
			DAS_pr("JMP 0x%04x\n\n", mem->read16(get_seg_adr(CS, eip)));
			eip += (s16)mem->read16(get_seg_adr(CS, eip)) + 2;
		} else {
			DAS_prt_post_op(4);
			DAS_pr("JMP 0x%08x\n\n", mem->read32(get_seg_adr(CS, eip)));
			eip += (s16)mem->read32(get_seg_adr(CS, eip)) + 4;
		}
		break;

/*
+--------+--------+--------+--------+--------+
|11101010| IP-lo  | IP-hi  | CS-lo  | CS-hi  |
+--------+--------+--------+--------+--------+
 */
	case 0xea: // セグメント外直接ジャンプ
		if (opsize == size16) {
			DAS_prt_post_op(4);
			warg1 = mem->read16(get_seg_adr(CS, eip));
			warg2 = mem->read16(get_seg_adr(CS, eip + 2));
			DAS_pr("JMP %04x:%04x\n\n", warg2, warg1);
			update_segreg(CS, warg2);
			eip = warg1;
		} else{
			DAS_prt_post_op(6);
			darg1 = mem->read32(get_seg_adr(CS, eip));
			warg2 = mem->read16(get_seg_adr(CS, eip + 4));
			DAS_pr("JMP %04x:%08x\n\n", warg2, darg1);
			update_segreg(CS, warg2);
			eip = darg1;
		}
		break;
/*
+--------+--------+
|11101011|IP-INC8 |
+--------+--------+
 */
	case 0xeb: //無条件ジャンプ/セグメントショート内直接
		DAS_prt_post_op(1);
		DAS_pr("JMP 0x%02x\n\n", mem->read8(get_seg_adr(CS, eip)));
		eip += (s8)mem->read8(get_seg_adr(CS, eip)) + 1;
		break;

/******************** TEST/NOT/NEG/MUL/IMUL/DIV/IDIV ********************/

	case 0xf6:
		modrm = mem->read8(get_seg_adr(CS, eip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + (subop < 2)?2:1);
		DAS_pr("%s ", strf6[subop]);
		DAS_modrm(modrm, false, (subop < 2)?false:true, byte);
		eip++;
		switch (subop) {
/*
          76  543 210
+--------+-----------+---------+---------+--------+-----------+
|1111011w|mod 000 r/m|(DISP-LO)|(DISP-HI)|  data  |data if w=1|
+--------+-----------+---------+---------+--------+-----------+
OF/CF:0, SF/ZF/PF:結果による, AF:未定義
 */
		case 0x0: // TEST r/m8, imm8
			// go through
		case 0x1:
			DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, eip + nr_disp_modrm(modrm))));
			flag8 = flag_calb[modrmb(modrm)
					  & mem->read8(get_seg_adr(CS, eip))];
			flagu8 &= ~OFSET8;
			eip++;
			break;
		case 0x2: // NOT r/m8
			if ((modrm & 0xc0) == 0xc0) {
				rm = modrm & 7;
				genregb(rm) = ~genregb(rm);
			} else {
				tmpadr = modrm_seg_ea(modrm);
				mem->write8(tmpadr, ~mem->read8(tmpadr));
			}
			break;
		case 0x3: // NEG r/m8
			if ((modrm & 0xc0) == 0xc0) {
				rm = modrm & 7;
				src = genregb(rm);
				res = 0 - src;
				genregb(rm) = (u8)res;
			} else {
				tmpadr = modrm_seg_ea(modrm);
				src = mem->read8(tmpadr);
				res = 0 - src;
				mem->write8(tmpadr, (u8)res);
			}
			flag8 = flag_calb[res & 0xff];
			(res == 0)? flag8 &= ~CF : flag8 |= CF;
			(res & src & 0x80)?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		case 0x4: // MUL r/m8
			if ((modrm & 0xc0) == 0xc0) {
				ax = genregb(modrm & 7) * al;
			} else {
				ax = mem->read8(modrm_seg_ea(modrm)) * al;
			}
			if (ah == 0) {
				flag8 = 0;
				flagu8 &= ~OFSET8;
			} else {
				flag8 = CF;
				flagu8 |= OFSET8;
			}
			break;
		case 0x5: // IMUL r/m8
			if ((modrm & 0xc0) == 0xc0) {
				ax = (s8)genregb(modrm & 7) * (s8)al;
			} else {
				ax = (s8)mem->read8(modrm_seg_ea(modrm)) * (s8)al;
			}
			if (ah == 0) {
				flag8 = 0;
				flagu8 &= ~OFSET8;
			} else {
				flag8 = CF;
				flagu8 |= OFSET8;
			}
			break;
		case 0x6: // DIV r/m8
			dst = ax;
			src = modrmb(modrm);
			al = dst / src;
			ah = dst % src;
			break;
		case 0x7: // IDIV r/m8
			dst = ax;
			src = modrmb(modrm);
			al = (s16)dst / (s8)src;
			ah = (s16)dst % (s8)src;
			break;
		}
		break;

	case 0xf7:
		modrm = mem->read8(get_seg_adr(CS, eip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + (subop < 2)?3:1);
		DAS_pr("%s ", strf6[subop]);
		DAS_modrm(modrm, false, (subop < 2)?false:true, word);
		eip++;
		switch (subop) {
		case 0x0: // TEST r/m16, imm16 (TEST r/m32, imm32)
			// go through
		case 0x1:
			DAS_pr("0x%04x\n\n", mem->read16(get_seg_adr(CS, eip + nr_disp_modrm(modrm))));
			flag8 = flag_calw[modrmw(modrm)
					  & mem->read16(get_seg_adr(CS, eip))];
			flagu8 &= ~OFSET8;
			eip += 2;
			break;
		case 0x2: // NOT r/m16
			if ((modrm & 0xc0) == 0xc0) {
				rm = modrm & 7;
				genregw(rm) = ~genregw(rm);
			} else {
				tmpadr = modrm_seg_ea(modrm);
				mem->write16(tmpadr, ~mem->read16(tmpadr));
			}
			break;
		case 0x3: // NEG r/m16
			if ((modrm & 0xc0) == 0xc0) {
				rm = modrm & 7;
				src = genregw(rm);
				res = 0 - src;
				genregw(rm) = (u16)res;
			} else {
				tmpadr = modrm_seg_ea(modrm);
				src = mem->read8(tmpadr);
				res = 0 - src;
				mem->write8(tmpadr, (u16)res);
			}
			flag8 = flag_calw[res & 0xffff];
			(res == 0)? flag8 &= ~CF : flag8 |= CF;
			(res & src & 0x8000)?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		case 0x4: // MUL r/m16
			if ((modrm & 0xc0) == 0xc0) {
				res = genregw(modrm & 7) * ax;
			} else {
				res = mem->read8(modrm_seg_ea(modrm)) * ax;
			}
			ax = res & 0xffff;
			dx = res >> 16;
			if (dx == 0) {
				flag8 = 0;
				flagu8 &= ~OFSET8;
			} else {
				flag8 = CF;
				flagu8 |= OFSET8;
			}
			break;
		case 0x5: // MUL r/m16
			if ((modrm & 0xc0) == 0xc0) {
				res = (s16)genregw(modrm & 7) * (s16)ax;
			} else {
				res = (s16)mem->read8(modrm_seg_ea(modrm)) * (s16)ax;
			}
			ax = res & 0xffff;
			dx = res >> 16;
			if (dx == 0) {
				flag8 = 0;
				flagu8 &= ~OFSET8;
			} else {
				flag8 = CF;
				flagu8 |= OFSET8;
			}
			break;
		case 0x6: // DIV r/m16
			dst = (u32)(dx << 16) + ax;
			src = modrmw(modrm);
			ax = dst / src;
			dx = dst % src;
			break;
		case 0x7: // IDIV r/m16
			dst = (u32)(dx << 16) + ax;
			src = modrmw(modrm);
			ax = (s32)dst / (s16)src;
			dx = (s32)dst % (s16)src;
			break;
		}
		break;

/******************** セグメントオーバーライド ********************/

#define SEG_OVRIDE(SEG)							\
	DAS_prt_post_op(0);						\
	DAS_pr("SEG="#SEG"\n\n");					\
		seg_ovride++;						\
		sdcr[DS].base = sdcr[SEG].base;				\
		sdcr[SS].base = sdcr[SEG].base;				\
		if (seg_ovride >= 8) { /* xxx ここら辺の情報不足*/	\
			/* xxx ソフトウェア例外 */ 			\
		}

	case 0x26: // SEG=ES
		SEG_OVRIDE(ES);
		return; // リターンする
	case 0x2e: // SEG=CS
		SEG_OVRIDE(CS);
		return; // リターンする
	case 0x36: // SEG=SS
		SEG_OVRIDE(SS);
		return; // リターンする
	case 0x3e: // SEG=DS
		SEG_OVRIDE(DS);
		return; // リターンする


/*************** オペランドサイズオーバーライドプリフィックス ***************/

	case 0x66:
		DAS_prt_post_op(0);
		DAS_pr("Ope Size Override\n\n");
		opsize_ovride = true;
		opsize = isRealMode? size32 : (sdcr[CS].attr & 0x400)? size16 : size32;
		return; // リターンする

/*************** アドレスサイズオーバーライドプリフィックス ***************/

	case 0x67:
		DAS_prt_post_op(0);
		DAS_pr("Addr Size Override\n\n");
		addrsize_ovride = true;
		addrsize = isRealMode? size32 : (sdcr[CS].attr & 0x400)? size16 : size32;
		return; // リターンする

/*************** LOCK ***************/

	case 0xf0:
		DAS_prt_post_op(0);
		DAS_pr("LOCK\n\n");
		// nothing to do
		break;

/*************** リピートプリフィックス ***************/

	case 0xf2:
		// repneでZFをチェックするのはCMPSとSCASのみ
		DAS_prt_post_op(0);
		DAS_pr("Repne Prefix\n\n");
		repne_prefix = true;
		return;
	case 0xf3:
		// repeでZFをチェックするのはCMPSとSCASのみ
		DAS_prt_post_op(0);
		DAS_pr("Repe Prefix\n\n");
		repe_prefix = true;
		return;

/******************** HLT ********************/

	case 0xf4:
#ifdef CORE_DAS
		if (!DAS_hlt) {
			DAS_prt_post_op(0);
			DAS_pr("HLT\n\n");
		}
		DAS_hlt = true; // xxx いつかfalseに戻す
#endif
		eip--;
		return; // リターンする？

/******************** プロセッサコントロール ********************/

	case 0xf5:
		DAS_prt_post_op(0);
		DAS_pr("CMC\n\n");
		(flag8 & CF)? flag8 &= ~CF : flag8 |= CF;
		break;
	case 0xf8:
		DAS_prt_post_op(0);
		DAS_pr("CLC\n\n");
		flag8 &= ~CF;
		break;
	case 0xf9:
		DAS_prt_post_op(0);
		DAS_pr("STC\n\n");
		flag8 |= CF;
		break;
	case 0xfa:
		DAS_prt_post_op(0);
		DAS_pr("CLI\n\n");
		flagu8 &= ~IFSET8;
		break;
	case 0xfb: // STI
		DAS_prt_post_op(0);
		DAS_pr("STI\n\n");
		flagu8 |= IFSET8;
		break;
	case 0xfc:
		DAS_prt_post_op(0);
		DAS_pr("CLD\n\n");
		flagu8 &= ~DFSET8;
		break;
	case 0xfd:
		DAS_prt_post_op(0);
		DAS_pr("STD\n\n");
		flagu8 |= DFSET8;
		break;

/******************** INC/DEC/CALL/JMP/PUSH ********************/

#define INC_RM(BWD, OP)						\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 7);			\
		res = dst OP 1;					\
		genreg##BWD(modrm & 7) = (BWD##CAST)res;	\
	} else {						\
		tmpadr = modrm_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		res = dst OP 1;					\
		mem->write##BWD(tmpadr, (BWD##CAST)res);	\
	}							\
	flag8 &= CF; /* CF以外はリセット*/			\
	flag8 |= flag_cal##BWD[res & BWD##ALLF];		\
	flag8 |= (dst ^ res) & AF;				\
	(dst ^ res) & BWD##MSB1?				\
		flagu8 |= OFSET8:flagu8 &= ~OFSET8;

	case 0xfe:
		modrm = mem->read8(get_seg_adr(CS, eip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strfe[subop]);
		DAS_modrm(modrm, false, true, byte);
		eip++;
		switch (subop) {
		case 0: // INC r/m8
			INC_RM(b, +);
			break;
		case 1: // DEC r/m8
			INC_RM(b, -);
			break;
		default:
			printf("xxxxx\n\n");
		}
		break;

	case 0xff: 
		modrm = mem->read8(get_seg_adr(CS, eip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strff[subop]);
		DAS_modrm(modrm, false, true, word);
		eip++;
		switch (subop) {
		case 0: // INC r/m16
			INC_RM(w, +);
			break;
		case 1: // DEC r/m16
			INC_RM(w, -);
			break;
		case 2: // CALL r/m16 (CALL r/m32) 絶対間接nearコール
			if ((modrm & 0xc0) == 0xc0) {
				PUSHW0(ip);
				eip = genregw(modrm & 7);
			} else {
				tmpadr = modrm_seg_ea(modrm);
				PUSHW0(ip);
				eip = mem->read16(tmpadr);
			}
			break;
		case 3: // CALL m16:16 (CALL m16:32) 絶対間接farコール
			if ((modrm & 0xc0) == 0xc0) {
				// xxx ソフトウェア例外らしい
			} else {
				tmpadr = modrm_seg_ea(modrm);
				warg1 = mem->read16(tmpadr);
				warg2 = mem->read16(tmpadr + 2);
				PUSHW0(segreg[CS]);
				PUSHW0(ip);
				update_segreg(CS, warg2);
				eip = warg1;
			}
			break;
		case 4: // JMPL r/m16 (JMP r/m32) 絶対間接nearジャンプ
			if ((modrm & 0xc0) == 0xc0) {
				eip = genregw(modrm & 7);
			} else {
				eip = mem->read16(modrm_seg_ea(modrm));
			}
			break;
		case 5: // JMPL r/m16 (JMP r/m32) 絶対間接faジャンプ
			if ((modrm & 0xc0) == 0xc0) {
				// xxx ソフトウェア例外らしい
			} else {
				tmpadr = modrm_seg_ea(modrm);
				update_segreg(CS, mem->read16(tmpadr + 2));
				eip = mem->read16(tmpadr);
			}
			break;
		case 6: // PUSH r/m16 (PUSH r/m32)
			if ((modrm & 0xc0) == 0xc0) {
				PUSHW_GENREG(genregw(modrm & 7));
			} else {
				PUSHW(mem->read16(modrm_seg_ea(modrm)));
			}
			break;
		default:
			printf("xxxxx\n\n");
		}
		break;

	default:
		DAS_prt_post_op(0);
		printf("xxxxxxxxxx\n\n");
	}

	if (seg_ovride > 0) {
		seg_ovride--;
		// オーバーライドしたセグメントを元に戻す
		if (seg_ovride == 0) {
			// xxx プロテクトモードでは元のbaseを入れないとだめでは?
			sdcr[DS].base = segreg[DS] << 4;
			sdcr[SS].base = segreg[SS] << 4;
		}
	}

	// {オペランド|アドレス}サイズオーバーライドプリフィックスを元に戻す
	opsize_ovride = false;
	addrsize_ovride = false;
	repne_prefix = false;
	repe_prefix = false;
	opsize = isRealMode? size16 : (sdcr[CS].attr & 0x400)? size32 : size16;
	addrsize = isRealMode? size16 : (sdcr[CS].attr & 0x400)? size32 : size16;
}
