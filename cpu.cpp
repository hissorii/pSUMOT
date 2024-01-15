#include <cstdio> // for printf()
#include "cpu.h"
#include "cpu_macros.h"
#include "cpu_clocks.h"

using namespace std; // for printf()

CPU::CPU(BUS* bus) {
	mem = bus->get_bus("mem");
	io = bus->get_bus("io");
	dmac = (DMAC *)bus->get_bus("dmac");

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
	// PFは下位1バイトあれば算出できるので、あらかじめ求めておく
	for (int i = 0; i < 0x100; i++) {
		u8 pf;
		pf = (u8)(i & 0xff); // パリティは下位8ビットのみチェックする
		pf ^= pf >> 4;
		pf ^= pf >> 2;
		pf ^= pf >> 1;
		pf = (pf & 1)? 0 : PF;
		pflag_cal[i] = pf;
	}
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

	remains_clks = 0;

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

	if (op_continue) {
		return;
	}

	printf("\n");
	for (i = 0; i < 4; i++) {
		printf("%s:%08x ", genreg_name[2][i], genregd(i));
	}
	printf("  eflags:%08x", eflagsu16 << 16 | flagu8 << 8 | flag8);
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
#if 0 // MAMEとの比較用に一時的にcr0を0にする
	printf("         cr0:%08x", cr[0]);
#else
	printf("         cr0:%08x", 0);
#endif
	printf("\n\n");

#if 0
	int j, startadr;
	if (step == 120) {
                for (i = 0; i < 32; i++) {
                        printf("0x%02x ", mem->read8(0xf7fb0 + i));
                        if (((i + 1) % 16) == 0) printf("\n");
                }
                printf("\n");
        }
	if (step == 50634) {
		startadr = 0xf7fc0;
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

	if (op_continue) {
		return;
	}
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
// eipはmodR/Mをポイントしていること
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
		printf("%s%s", genreg_name[regsize][rm], isDest?"\n":", ");
		if (isReg && !isDest) {
			reg = modrm >> 3 & 7;
			printf("%s\n", genreg_name[regsize][reg]);
		}
		return;
	}

	printf("%s ptr ", sizestr[regsize]);

	// [disp16]
	if (addrsize == size16 && rm == 6 && mod == 0) {
		printf("[0x%04x]%s",
		       mem->read16(get_seg_adr(CS, eip + 1)),
		       isDest?"\n":", ");
		return;
	}

	// [disp32]
	if (addrsize == size32 && rm == 5 && mod == 0) {
		printf("[0x%08x]%s",
		       mem->read32(get_seg_adr(CS, eip + 1)),
		       isDest?"\n":", ");
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
			sprintf(disp, " + 0x%04x",
				mem->read16(get_seg_adr(CS, ++tip)));
		} else {
			sprintf(disp, " + 0x%08x",
				mem->read32(get_seg_adr(CS, ++tip)));
		}
	} else {
		disp[0] = '\0';
	}
	printf("%s%s]%s",
	       addressing_str[addrsize][rm], disp, isDest?"\n":", ");

	if (isReg && !isDest) {
		reg = modrm >> 3 & 7;
		printf("%s\n", genreg_name[regsize][reg]);
	}
}

#define DAS_pr(...) if (!op_continue) printf(__VA_ARGS__)
#define OP_CONTINUE() op_continue = true

#else
#define DAS_dump_reg()
#define DAS_prt_post_op(n)
#define DAS_modrm(m, isR, isD, isW)
#define DAS_pr(...)
#define OP_CONTINUE()
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

	// addrsize32
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

	if (mod == 1) {
		tmp16 += (s8)mem->read8(get_seg_adr(CS, eip));
		eip++;
	} else if (mod == 2) {
		tmp16 += (s16)mem->read16(get_seg_adr(CS, eip));
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

	if (mod == 1) {
		tmp32 += (s8)mem->read8(get_seg_adr(CS, eip));
		eip++;
	} else if (mod == 2) {
		tmp32 += (s32)mem->read32(get_seg_adr(CS, eip));
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
	}

	// xxxSIBのベースがもしEBPだったらセグメントはCS?
	if ((modrm & 7) != 4) {
		return modrm32_ea(modrm) +
			sdcr[modrm_add_seg[1][modrm >> 6][modrm & 7]].base;
	} else {
		u8 sib;
		sib = get_seg_adr(CS, eip);
		return modrm32_ea(modrm) + sdcr[modrm_add_sib[sib & 7]].base;
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

#define CLKS(clk_op) clks -= (clk_op)

s32 CPU::exec(void) {
	u8 op, subop;
	u16 warg1, warg2;
	u32 darg1;
	u8 modrm, ndisp, sreg, greg, rm;
	u8 tmpb;
	u32 tmpadr;
	u32 src, dst, res;
	u64 dst64;
	u32 cnt;
	s32 incdec;

	clks = remains_clks;
	while (clks > exit_clks) { // xxx マイナスになった分はどこかで補填する?

#if 0	// MAMEとの比較用に一時的に変更
		if (seg_ovride == 0 && !opsize_ovride && !addrsize_ovride &&!repe_prefix && !repne_prefix) {
			DAS_dump_reg();
		}
#else
		DAS_dump_reg();
#endif

		if (dmac->working) {
		}

		// リアルモードでip++した時に16bitをこえて0に戻る場合を考慮し、
		// リアルモードの場合はeip++ではなくip++するようにした。
		// ここまでする考慮しなくてもよければ削る(高速化のため)
		op = mem->read8(get_seg_adr(CS, isRealMode? ip++ : eip++));
		DAS_pr("%08x %02x", get_seg_adr(CS, eip - 1), op);

		switch (op) {

/******************** ADD ********************/
/*
  +--------+-----------+---------+---------+
  |000000dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
  +--------+-----------+---------+---------+
  OF/SF/ZF/AF/PF/CF:結果による
*/
		case 0x00: // ADD r/m8, r8
			CAL_RM_R(ADD, b, 0);
			break;
		case 0x01: // ADD r/m16, r16 (ADD r/m32, r32)
			if (opsize == size16) {
				CAL_RM_R(ADD, w, 0);
			} else {
				CAL_RM_R(ADD, d, 0);
			}
			break;
		case 0x02: // ADD r8, r/m8
			CAL_R_RM(ADD, b, 0);
			break;
		case 0x03: // ADD r16, r/m16 (ADD r32, r/m32)
			if (opsize == size16) {
				CAL_R_RM(ADD, w, 0);
			} else {
				CAL_R_RM(ADD, d, 0);
			}
			break;
		case 0x04: // ADD AL, imm8
			CLKS(CLK_CAL_R_IMM);
			DAS_prt_post_op(1);
			src = mem->read8(get_seg_adr(CS, eip));
			DAS_pr("ADD AL, 0x%02x\n", src);
			eip++;
			res = al + src;
			FLAG8bADD(res, src, al, );
			OF_ADDb(res, src, al);
			al = res;
			break;
		case 0x05: // ADD AX, imm16 (ADD EAX, imm32)
			CLKS(CLK_CAL_R_IMM);
			if (opsize == size16) {
				DAS_prt_post_op(2);
				src = mem->read16(get_seg_adr(CS, eip));
				DAS_pr("ADD AX, 0x%04x\n", src);
				eip += 2;
				res = ax + src;
				FLAG8wADD(res, src, ax, );
				OF_ADDw(res, src, ax);
				ax = res;
			}else {
				DAS_prt_post_op(4);
				src = mem->read32(get_seg_adr(CS, eip));
				DAS_pr("ADD EAX, 0x%08x\n", src);
				eip += 4;
				res = eax + src;
				FLAG8dADD(res, src, eax, );
				OF_ADDd(res, src, eax);
				eax = res;
			}
			break;

/******************** ADC ********************/

		case 0x10: // ADC r/m8, r8
			CAL_RM_R(ADC, b, (flag8 & CF));
			break;
		case 0x11: // ADC r/m16, r16 (ADC r/m32, r32)
			if (opsize == size16) {
				CAL_RM_R(ADC, w, (flag8 & CF));
			} else {
				CAL_RM_R(ADC, d, (flag8 & CF));
			}
			break;
		case 0x12: // ADC r8, r/m8
			CAL_R_RM(ADC, b, (flag8 & CF));
			break;
		case 0x13: // ADC r16, r/m16 (ADC r32, r/m32)
			if (opsize == size16) {
				CAL_R_RM(ADC, w, (flag8 & CF));
			} else {
				CAL_R_RM(ADC, d, (flag8 & CF));
			}
			break;
		case 0x14: // ADC AL, imm8
			CLKS(CLK_CAL_R_IMM);
			DAS_prt_post_op(1);
			src = mem->read8(get_seg_adr(CS, eip));
			DAS_pr("ADC AL, 0x%02x\n", src);
			eip++;
			res = al + src + (flag8 & CF);
			FLAG8bADC(res, src, al, );
			OF_ADCb(res, src, al);
			al = res;
			break;
		case 0x15: // ADC AX, imm16 (ADC EAX, imm32)
			CLKS(CLK_CAL_R_IMM);
			if (opsize == size16) {
				DAS_prt_post_op(2);
				src = mem->read16(get_seg_adr(CS, eip));
				DAS_pr("ADC AX, 0x%04x\n", src);
				eip += 2;
				res = ax + src + (flag8 & CF);
				FLAG8wADC(res, src, ax, );
				OF_ADCw(res, src, ax);
				ax = res;
			} else {
				DAS_prt_post_op(4);
				src = mem->read32(get_seg_adr(CS, eip));
				DAS_pr("ADC EAX, 0x%08x\n", src);
				eip += 4;
				res = eax + src + (flag8 & CF);
				FLAG8dADC(res, src, eax, (flag8 & CF));
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
			CLKS(CLK_CAL_R_IMM);
			DAS_prt_post_op(1);
			src = mem->read8(get_seg_adr(CS, eip));
			DAS_pr("OR AL, 0x%02x\n", src);
			al |= src;
			FLAG_LOGOPb(al);
			eip++;
			break;
		case 0x0d: // OR AX, imm16 (OR EAX, imm32)
			CLKS(CLK_CAL_R_IMM);
			if (opsize == size16) {
				DAS_prt_post_op(2);
				src = mem->read16(get_seg_adr(CS, eip));
				DAS_pr("OR AX, 0x%04x\n", src);
				ax |= src;
				FLAG_LOGOPw(ax);
				eip += 2;
			} else {
				DAS_prt_post_op(4);
				src = mem->read32(get_seg_adr(CS, eip));
				DAS_pr("OR EAX, 0x%08x\n", src);
				eax |= src;
				FLAG_LOGOPd(eax);
				eip += 4;
			}
			break;

/******************** PUSH ********************/
// xxxセグメントオーバーライドされていても、
// call, pusha, enterではSSを使うらしい

		case 0x06: // PUSH ES
			CLKS(CLK_PUSH_SR);
			PUSH_SEG(ES);
			break;
		case 0x0e: // PUSH CS
			CLKS(CLK_PUSH_SR);
			PUSH_SEG(CS);
			break;
		case 0x16: // PUSH SS
			CLKS(CLK_PUSH_SR);
			PUSH_SEG(SS);
			break;
		case 0x1e: // PUSH DS
			CLKS(CLK_PUSH_SR);
			PUSH_SEG(DS);
			break;

		case 0x50: // PUSH AX (PUSH EAX)
			CLKS(CLK_PUSH_R);
			if (opsize == size16) {
				PUSHW_GENREG(ax);
			} else {
				PUSHD_GENREG(eax);
			}
			break;
		case 0x51: // PUSH CX (PUSH ECX)
			CLKS(CLK_PUSH_R);
			if (opsize == size16) {
				PUSHW_GENREG(cx);
			} else {
				PUSHD_GENREG(ecx);
			}
			break;
		case 0x52: // PUSH DX (PUSH EDX)
			CLKS(CLK_PUSH_R);
			if (opsize == size16) {
				PUSHW_GENREG(dx);
			} else {
				PUSHD_GENREG(edx);
			}
			break;
		case 0x53: // PUSH BX (PUSH EBX)
			CLKS(CLK_PUSH_R);
			if (opsize == size16) {
				PUSHW_GENREG(bx);
			} else {
				PUSHD_GENREG(ebx);
			}
			break;
		case 0x54: // PUSH SP (PUSH ESP)
			CLKS(CLK_PUSH_R);
			if (opsize == size16) {
				PUSHW_GENREG(sp);
			} else {
				PUSHD_GENREG(esp);
			}
			break;
		case 0x55: // PUSH BP (PUSH EBP)
			CLKS(CLK_PUSH_R);
			if (opsize == size16) {
				PUSHW_GENREG(bp);
			} else {
				PUSHD_GENREG(ebp);
			}
			break;
		case 0x56: // PUSH SI (PUSH ESI)
			CLKS(CLK_PUSH_R);
			if (opsize == size16) {
				PUSHW_GENREG(si);
			} else {
				PUSHD_GENREG(esi);
			}
			break;
		case 0x57: // PUSH DI (PUSH EDI)
			CLKS(CLK_PUSH_R);
			if (opsize == size16) {
				PUSHW_GENREG(di);
			} else {
				PUSHD_GENREG(edi);
			}
			break;

		case 0x60: // PUSHA (PUSHAD)
			CLKS(CLK_PUSHA);
			DAS_prt_post_op(0);
			if (opsize == size16) {
				DAS_pr("PUSHA\n");
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
				DAS_pr("PUSHAD\n");
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
			CLKS(CLK_PUSH_IMM);
			if (opsize == size16) {
				DAS_prt_post_op(2);
				dst = mem->read16(get_seg_adr(CS, eip));
				DAS_pr("PUSH 0x%04x\n", dst);
				PUSHW(dst);
				eip += 2;
			} else {
				DAS_prt_post_op(4);
				dst = mem->read32(get_seg_adr(CS, eip));
				DAS_pr("PUSH 0x%08x\n", dst);
				PUSHD(dst);
				eip += 4;
			}
			break;

		case 0x6a: // PUSH imm8
			CLKS(CLK_PUSH_IMM);
			DAS_prt_post_op(2);
			dst = mem->read8(get_seg_adr(CS, eip));
			DAS_pr("PUSH 0x%02x\n", dst);
			PUSH(dst);
			eip += 1;
			break;

		case 0x9c: // PUSHF (PUSHFD)
			CLKS(CLK_PUSHF);
			DAS_prt_post_op(0);
			if (opsize == size16) {
				DAS_pr("PUSHF\n");
				PUSHW(flagu8 << 8 | flag8);
			} else {
				DAS_pr("PUSHFD\n");
				PUSHD(eflagsu16 << 16 | flagu8 << 8 | flag8);
			}
			break;

/******************** POP ********************/
// xxxセグメントオーバーライドされていても、
// call, pusha, enterではSSを使うらしい

		case 0x07: // POP ES
			CLKS(isRealMode?CLK_POP_SR:CLK_PM_POP_SR);
			POP_SEG(ES);
			break;
		case 0x17: // POP SS
			CLKS(isRealMode?CLK_POP_SR:CLK_PM_POP_SR);
			POP_SEG(SS);
			break;
		case 0x1f: // POP DS
			CLKS(isRealMode?CLK_POP_SR:CLK_PM_POP_SR);
			POP_SEG(DS);
			break;

		case 0x58: // POP AX (POP EAX)
			CLKS(CLK_POP_R);
			if (opsize == size16) {
				POPW_GENREG(ax);
			} else {
				POPD_GENREG(eax);
			}
			break;
		case 0x59: // POP CX (POP ECX)
			CLKS(CLK_POP_R);
			if (opsize == size16) {
				POPW_GENREG(cx);
			} else {
				POPD_GENREG(ecx);
			}
			break;
		case 0x5a: // POP DX (POP EDX)
			CLKS(CLK_POP_R);
			if (opsize == size16) {
				POPW_GENREG(dx);
			} else {
				POPD_GENREG(edx);
			}
			break;
		case 0x5b: // POP BX (POP EBX)
			CLKS(CLK_POP_R);
			if (opsize == size16) {
				POPW_GENREG(bx);
			} else {
				POPD_GENREG(ebx);
			}
			break;
		case 0x5c: // POP SP (POP ESP)
			CLKS(CLK_POP_R);
			if (opsize == size16) {
				POPW_GENREG(sp);
			} else {
				POPD_GENREG(esp);
			}
			break;
		case 0x5d: // POP BP (POP EBP)
			CLKS(CLK_POP_R);
			if (opsize == size16) {
				POPW_GENREG(bp);
			} else {
				POPD_GENREG(ebp);
			}
			break;
		case 0x5e: // POP SI (POP ESI)
			CLKS(CLK_POP_R);
			if (opsize == size16) {
				POPW_GENREG(si);
			} else {
				POPD_GENREG(esi);
			}
			break;
		case 0x5f: // POP DI (POP EDI)
			CLKS(CLK_POP_R);
			if (opsize == size16) {
				POPW_GENREG(di);
			} else {
				POPD_GENREG(edi);
			}
			break;

		case 0x61: // POPA (POPAD)
			CLKS(CLK_POPA);
			DAS_prt_post_op(0);
			if (opsize == size16) {
				DAS_pr("POPA\n");
				POPW0(di);
				POPW0(si);
				POPW0(bp);
				sp += 2;
				POPW0(bx);
				POPW0(dx);
				POPW0(cx);
				POPW0(ax);
			} else {
				DAS_pr("POPAD\n");
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
			CLKS(CLK_POP_MEM);
			if (opsize == size16) {
				modrm = mem->read8(get_seg_adr(CS, eip));
				DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
				DAS_pr("POP ");
				DAS_modrm(modrm, false, true, word);
				eip++;
				if ((modrm & 0xc0) == 0xc0) {
					genregw(modrm & 7) = mem->read16(get_seg_adr(SS, sp));
				} else {
					mem->write16(modrm_seg_ea(modrm), mem->read16(get_seg_adr(SS, sp)));
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
					mem->write32(modrm_seg_ea(modrm), mem->read32(get_seg_adr(SS, esp)));
				}
				sp += 4;
			}
			break;

		case 0x9d: // POPF (POPFD)
			CLKS(CLK_POPF);
			DAS_prt_post_op(0);
			if (opsize == size16) {
				DAS_pr("POPF\n");
				POPW(dst);
				flagu8 = (u8)(dst >> 8);
				flag8  = dst & 0xff;
			} else {
				DAS_pr("POPFD\n");
				POPD(dst);
				eflagsu16 = (u16)(dst >> 16);
				flagu8 = (u8)(dst >> 8);
				flag8  = dst & 0xff;
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
		case 0x24: // AND al, imm8
			CLKS(CLK_CAL_R_IMM);
			DAS_prt_post_op(1);
			DAS_pr("AND AL, 0x%02x\n", mem->read8(get_seg_adr(CS, eip)));
			al &= mem->read8(get_seg_adr(CS, eip++));
			FLAG_LOGOPb(al);
			break;
		case 0x25: // AND AX, imm16 (AND EAX, imm32)
			CLKS(CLK_CAL_R_IMM);
			DAS_prt_post_op(opsize == size16?2:4);
			DAS_pr((opsize == size16)?"AND AX, 0x%04x\n":"AND EAX, 0x%08x\n", (opsize == size16)?mem->read16(get_seg_adr(CS, eip)):mem->read32(get_seg_adr(CS, eip)));
			if (opsize == size16) {
				ax &= mem->read16(get_seg_adr(CS, eip));
				FLAG_LOGOPw(ax);
				eip += 2;
			} else {
				eax &= mem->read32(get_seg_adr(CS, eip));
				eip += 4;
				FLAG_LOGOPd(eax);
			}
			break;

/******************** DAA/DAS/AAA/AAS/AAM/AAD ********************/

		case 0x27:
			CLKS(CLK_DAA);
			DAS_prt_post_op(0);
			DAS_pr("DAA\n");
			tmpb = flag8 & CF;
			res = al;
			if (flag8 & AF || (al & 0x0f) > 9) {
				res += 6;
				flag8 = AF; // AF以外はリセット
			} else {
				flag8 = 0; // AFも含めてリセット
			}
			// ここの比較はresでなくalで正しい
			if (tmpb || al > 0x99) {
				res += 0x60;
				flag8 |= CF;
			}
			al = (u8)res;
			// CFは確定しているの判定は8bitで行う
			flag8 |= flag_calb[al];
			break;
		case 0x2f:
			CLKS(CLK_DAS);
			DAS_prt_post_op(0);
			DAS_pr("DAS\n");
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
			CLKS(CLK_AAA);
			DAS_prt_post_op(0);
			DAS_pr("AAA\n");
			if (flag8 & AF || (al & 0x0f) > 9) {
				al = (al + 6) & 0x0f;
				ah++;
				// AF, CF以外はリセット(他は未定義)
				flag8 = AF & CF;
			} else {
				// CF, AFも含めてリセット(他は未定義)
				flag8 = 0;
			}
			break;
		case 0x3f:
			CLKS(CLK_AAS);
			DAS_prt_post_op(0);
			DAS_pr("AAS\n");
			if (flag8 & AF || (al & 0x0f) > 9) {
				al = (al - 6) & 0x0f;
				ah--;
				flag8 = AF & CF;
			} else {
				flag8 = 0;
			}
			break;
		case 0xd4:
			CLKS(CLK_AAM);
			DAS_prt_post_op(1);
			tmpb = mem->read8(get_seg_adr(CS, eip));
			if (tmpb != 0x0a) {
				// imm8が0x0aでなければ本当はミーモニックなし
				DAS_pr("AAM 0x%02x\n", tmpb);
			} else {
				DAS_pr("AAM\n");
			}
			eip++;
			ah = al / tmpb;
			al = al % tmpb;
			flag8 = flag_calb[al]; // CFは未定義(OF, AFも未定義)
			break;
		case 0xd5:
			CLKS(CLK_AAD);
			DAS_prt_post_op(1);
			tmpb = mem->read8(get_seg_adr(CS, eip));
			if (tmpb != 0x0a) {
				DAS_pr("AAD 0x%02x\n", tmpb);
			} else {
				DAS_pr("AAD\n");
			}
			eip++;
			al += (ah + tmpb) & 0xff;
			ah = 0;
			flag8 = flag_calb[al];
			break;

/******************** SBB ********************/

		case 0x18: // SBB r/m8, r8
			CAL_RM_R(SBB, b, -(flag8 & CF));
			break;
		case 0x19: // SBB r/m16, r16 (SBB r/m32, r32)
			if (opsize == size16) {
				CAL_RM_R(SBB, w, -(flag8 & CF));
			} else {
				CAL_RM_R(SBB, d, -(flag8 & CF));
			}
			break;
		case 0x1a: // SBB r8, r/m8
			CAL_R_RM(SBB, b, -(flag8 & CF));
			break;
		case 0x1b: // SBB r16, r/m16 (SBB r32, r/m32)
			if (opsize == size16) {
				CAL_R_RM(SBB, w, -(flag8 & CF));
			} else {
				CAL_R_RM(SBB, d, -(flag8 & CF));
			}
			break;
		case 0x1c: // SBB AL, imm8
			CLKS(CLK_CAL_R_IMM);
			DAS_prt_post_op(1);
			DAS_pr("SBB AL, 0x%02x\n", mem->read8(get_seg_adr(CS, eip)));
			dst = al;
			src = mem->read8(get_seg_adr(CS, eip));
			res = dst - src - (flag8 & CF);
			al = (u8)res;
			eip ++;
			FLAG8bSBB(res, src, dst, );
			OF_SBBb(res, src, dst);
			break;
		case 0x1d: // SBB AX, imm16 (SBB EAX, imm32)
			CLKS(CLK_CAL_R_IMM);
			if (opsize == size16) {
				DAS_prt_post_op(2);
				src = mem->read16(get_seg_adr(CS, eip));
				DAS_pr("SBB AX, 0x%04x\n", src);
				dst = ax;
				res = dst - src - (flag8 & CF);
				ax = (u16)res;
				eip += 2;
				FLAG8wSBB(res, src, dst, );
				OF_SBBw(res, src, dst);
			} else {
				DAS_prt_post_op(4);
				src = mem->read32(get_seg_adr(CS, eip));
				DAS_pr("SBB EAX, 0x%08x\n", src);
				dst = eax;
				res = dst - src - (flag8 & CF);
				eax = res;
				eip += 4;
				FLAG8dSBB(res, src, dst, (flag8 & CF));
				OF_SBBd(res, src, dst);
			}
			break;

/******************** SUB ********************/

		case 0x28: // SUB r/m8, r8
			CAL_RM_R(SUB, b, 0);
			break;
		case 0x29: // SUB r/m16, r16 (SUB r/m32, r32)
			if (opsize == size16) {
				CAL_RM_R(SUB, w, 0);
			} else {
				CAL_RM_R(SUB, d, 0);
			}
			break;
		case 0x2a: // SUB r8, r/m8
			CAL_R_RM(SUB, b, 0);
			break;
		case 0x2b: // SUB r16, r/m16 (SUB r32, r/m32)
			if (opsize == size16) {
				CAL_R_RM(SUB, w, 0);
			} else {
				CAL_R_RM(SUB, d, 0);
			}
			break;
		case 0x2c: // SUB AL, imm8
			CLKS(CLK_CAL_R_IMM);
			DAS_prt_post_op(1);
			src = mem->read8(get_seg_adr(CS, eip));
			DAS_pr("SUB AL, 0x%02x\n", src);
			dst = al;
			res = dst - src;
			al = (u8)res;
			eip ++;
			FLAG8bSUB(res, src, dst, );
			OF_SUBb(res, src, dst);
			break;
		case 0x2d: // SUB AX, imm16 (SUB EAX, imm32)
			CLKS(CLK_CAL_R_IMM);
			if (opsize == size16) {
				DAS_prt_post_op(2);
				src = mem->read16(get_seg_adr(CS, eip));
				DAS_pr("SUB AX, 0x%04x\n", src);
				dst = ax;
				res = dst - src;
				ax = (u16)res;
				eip += 2;
				FLAG8wSUB(res, src, dst, );
				OF_SUBw(res, src, dst);
			} else {
				DAS_prt_post_op(4);
				src = mem->read32(get_seg_adr(CS, eip));
				DAS_pr("SUB EAX, 0x%08x\n", src);
				dst = eax;
				res = dst - src;
				eax = res;
				eip += 4;
				FLAG8dSUB(res, src, dst, );
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
		case 0x33: // XOR r16, r/m16 (XOR r32, r/m32)
			if (opsize == size16) {
				LOGOP_R_RM(^, XOR, w);
			} else {
				LOGOP_R_RM(^, XOR, d);
			}
			break;
		case 0x34: // XOR AL, imm8
			CLKS(CLK_CAL_R_IMM);
			DAS_prt_post_op(1);
			DAS_pr("XOR AL, 0x%02x\n", mem->read8(get_seg_adr(CS, eip)));
			al ^= mem->read8(get_seg_adr(CS, eip));
			eip++;
			flag8 = flag_calb[al];
			flagu8 &= ~OFSET8;
			break;
		case 0x35: // XOR AX, imm16 (XOR EAX, imm32)
			CLKS(CLK_CAL_R_IMM);
			if (opsize == size16) {
				DAS_prt_post_op(2);
				src = mem->read16(get_seg_adr(CS, eip));
				DAS_pr("XOR AX, 0x%04x\n", src);
				ax ^= src;
				eip += 2;
				FLAG_LOGOPw(ax);
			} else {
				DAS_prt_post_op(4);
				src = mem->read32(get_seg_adr(CS, eip));
				DAS_pr("XOR EAX, 0x%08x\n", src);
				eax ^= src;
				eip += 4;
				FLAG_LOGOPd(eax);
			}
			break;

/******************** CMP ********************/

/*
  3C ib
  CF/OF/SF/ZF/AF/PF:結果による
*/
		case 0x38: // CMP r/m8, r8
			CMP_RM_R(b);
			break;
		case 0x39: // CMP r/m16, r16 (CMP r/m32, r32)
			if (opsize == size16) {
				CMP_RM_R(w);
			} else {
				CMP_RM_R(d);
			}
			break;
		case 0x3A: // CMP r8, r/m8
			CMP_R_RM(b);
			break;
		case 0x3B: // CMP r16, r/m16 (CMP r32, r/m32)
			if (opsize == size16) {
				CMP_R_RM(w);
			} else {
				CMP_R_RM(d);
			}
			break;
		case 0x3c: // CMP AL, imm8
			CLKS(CLK_CMP_R_IMM);
			DAS_prt_post_op(1);
			src = mem->read8(get_seg_adr(CS, eip));
			DAS_pr("CMP AL, 0x%02x\n", src);
			res = al - src;
			eip++;
			flag8 = flag_calb[res & 0x1ff];
			flag8 |= (al ^ src ^ res) & AF;
			(al ^ res) & (al ^ src) & 0x80?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		case 0x3d: // CMP AX, imm16 (CMP EAX, imm32)
			CLKS(CLK_CMP_R_IMM);
			if (opsize == size16) {
				DAS_prt_post_op(2);
				src = mem->read16(get_seg_adr(CS, eip));
				DAS_pr("CMP AX, 0x%04x\n", src);
				res = ax - src;
				eip += 2;
				FLAG8wSUB(res, src, ax, );
				OF_SUBw(res, src, ax);
			} else {
				DAS_prt_post_op(4);
				src = mem->read32(get_seg_adr(CS, eip));
				DAS_pr("CMP EAX, 0x%08x\n", src);
				res = eax - src;
				eip += 4;
				FLAG8dSUB(res, src, eax, );
				OF_SUBd(res, src, eax);
			}
			break;


/******************** INC ********************/
/*
  CF:影響なし, OF/SF/ZF/AF/PF:結果による
*/
		case 0x40: // INC AX (INC EAX)
			CLKS(CLK_INCDEC_R);
			if (opsize == size16) {
				INC_R16(ax);
			} else {
				INC_R32(eax);
			}
			break;
		case 0x41: // INC CX (INC ECX)
			CLKS(CLK_INCDEC_R);
			if (opsize == size16) {
				INC_R16(cx);
			} else {
				INC_R32(ecx);
			}
			break;
		case 0x42: // INC DX (INC EDX)
			CLKS(CLK_INCDEC_R);
			if (opsize == size16) {
				INC_R16(dx);
			} else {
				INC_R32(edx);
			}
			break;
		case 0x43: // INC BX (INC EBX)
			CLKS(CLK_INCDEC_R);
			if (opsize == size16) {
				INC_R16(bx);
			} else {
				INC_R32(ebx);
			}
			break;
		case 0x44: // INC SP (INC ESP)
			CLKS(CLK_INCDEC_R);
			if (opsize == size16) {
				INC_R16(sp);
			} else {
				INC_R32(esp);
			}
			break;
		case 0x45: // INC BP (INC EBP)
			CLKS(CLK_INCDEC_R);
			if (opsize == size16) {
				INC_R16(bp);
			} else {
				INC_R32(ebp);
			}
			break;
		case 0x46: // INC SI (INC ESI)
			CLKS(CLK_INCDEC_R);
			if (opsize == size16) {
				INC_R16(si);
			} else {
				INC_R32(esi);
			}
			break;
		case 0x47: // INC DI (INC EDI)
			CLKS(CLK_INCDEC_R);
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
		case 0x48: // DEC AX (DEC EAX)
			CLKS(CLK_INCDEC_R);
			if (opsize  == size16) {
				DEC_R16(ax);
			} else {
				DEC_R32(eax);
			}
			break;
		case 0x49: // DEC CX (DEC ECX)
			CLKS(CLK_INCDEC_R);
			if (opsize  == size16) {
				DEC_R16(cx);
			} else {
				DEC_R32(ecx);
			}
			break;
		case 0x4a: // DEC DX (DEC EDX)
			CLKS(CLK_INCDEC_R);
			if (opsize  == size16) {
				DEC_R16(dx);
			} else {
				DEC_R32(edx);
			}
			break;
		case 0x4b: // DEC BX (DEC EBX)
			CLKS(CLK_INCDEC_R);
			if (opsize  == size16) {
				DEC_R16(bx);
			} else {
				DEC_R32(ebx);
			}
			break;
		case 0x4c: // DEC SP (DEC ESP)
			CLKS(CLK_INCDEC_R);
			if (opsize  == size16) {
				DEC_R16(sp);
			} else {
				DEC_R32(esp);
			}
			break;
		case 0x4d: // DEC BP (DEC EBP)
			CLKS(CLK_INCDEC_R);
			if (opsize  == size16) {
				DEC_R16(bp);
			} else {
				DEC_R32(ebp);
			}
			break;
		case 0x4e: // DEC SI (DEC ESI)
			CLKS(CLK_INCDEC_R);
			if (opsize  == size16) {
				DEC_R16(si);
			} else {
				DEC_R32(esi);
			}
			break;
		case 0x4f: // DEC DI (DEC EDI)
			CLKS(CLK_INCDEC_R);
			if (opsize  == size16) {
				DEC_R16(di);
			} else {
				DEC_R32(edi);
			}
			break;

		case 0x0f:
			subop = mem->read8(get_seg_adr(CS, eip));
			switch (subop) {
			case 0x01: // LGDT/LIDT
				CLKS(CLK_LGDT_LIDT);
				DAS_prt_post_op(2);
				dst = mem->read16(get_seg_adr(CS, ++eip));
				DAS_pr("%s ", (dst >> 3 & 7) == 2?"LGDT":"LIDT");
				DAS_modrm(dst, false, true, fword);
				if ((dst >> 3 & 7) == 2) { // LGDT
					tmpadr = modrm_seg_ea(dst);
					gdtr.limit = mem->read16(tmpadr);
					gdtr.base = (opsize == size16)? mem->read32(tmpadr + 2) & 0x00ffffff : mem->read32(tmpadr + 2);
				} else if ((dst >> 3 & 7) == 3) { // LIDT
					DAS_pr("xxxxx\n");
				}
				eip++;
				break;
			case 0x20: // MOV r32, CR0
				CLKS(CLK_MOV_R_CR);
				DAS_prt_post_op(2);
				modrm = mem->read8(get_seg_adr(CS, ++eip));
				DAS_pr("MOV ");
				DAS_modrm(modrm, false, false, dword);
				tmpb = modrm >> 3 & 7;
				DAS_pr("CR%d\n", tmpb);
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
						// リアルモードでeipを使うので
						// 上位ビットをクリアしておく
						// xxx 実際はどうなるのだろう
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
				CLKS(CLK_PUSH_SR);
				PUSH_SEG2(FS); // 2バイト命令用マクロ
				break;
			case 0xa1: // POP FS
				CLKS(isRealMode?CLK_POP_SR:CLK_PM_POP_SR);
				POP_SEG2(FS);
				break;
			case 0xa8: // PUSH GS
				CLKS(CLK_PUSH_SR);
				PUSH_SEG2(GS);
				break;
			case 0xa9: // POP GS
				CLKS(isRealMode?CLK_POP_SR:CLK_PM_POP_SR);
				POP_SEG2(GS);
				break;
			case 0xb6: // MOVZX r16,r/m8 (MOVZX r32, r/m8)
				modrm = mem->read8(get_seg_adr(CS, eip + 1));
				DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
				eip++;
				DAS_pr("MOVZX ");
				// xxx 現状 MOVZX SP, AXの様にsrcが
				// 16bit表記になる
				DAS_modrm(modrm, true, true, (opsize == size16)?word:dword);
				eip++;
				if (opsize == size16) {
					if ((modrm & 0xc0) == 0xc0) {
						CLKS(CLK_MOVZX_R_R);
						genregw(modrm >> 3 & 7) = genregb(modrm & 7);
					} else {
						CLKS(CLK_MOVZX_R_MEM);
						genregw(modrm >> 3 & 7) = mem->read8(modrm_seg_ea(modrm));
					}
				} else {
					if ((modrm & 0xc0) == 0xc0) {
						CLKS(CLK_MOVZX_R_R);
						genregd(modrm >> 3 & 7) = genregb(modrm & 7);
					} else {
						CLKS(CLK_MOVZX_R_MEM);
						genregd(modrm >> 3 & 7) = mem->read8(modrm_seg_ea(modrm));
					}
				}
				break;
			case 0xb7: // MOVZX r32,r/m16
				modrm = mem->read8(get_seg_adr(CS, eip + 1));
				DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
				eip++;
				DAS_pr("MOVZX ");
				// xxx 現状 MOVZX ESP, EAXの様にsrcが
				// 32bit表記になる
				DAS_modrm(modrm, true, true, dword);
				eip++;
				if ((modrm & 0xc0) == 0xc0) {
					CLKS(CLK_MOVZX_R_R);
					genregd(modrm >> 3 & 7) = genregw(modrm & 7);
				} else {
					CLKS(CLK_MOVZX_R_MEM);
					genregd(modrm >> 3 & 7) = mem->read16(modrm_seg_ea(modrm));
				}
				break;
			case 0xbe: // MOVSX r16,r/m8 (MOVSX r32, r/m8)
				modrm = mem->read8(get_seg_adr(CS, eip + 1));
				DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
				eip++;
				DAS_pr("MOVSX ");
				// xxx 現状 MOVZX SP, AXの様にsrcが
				// 16bit表記になる
				DAS_modrm(modrm, true, true, (opsize == size16)?word:dword);
				eip++;
				if (opsize == size16) {
					if ((modrm & 0xc0) == 0xc0) {
						CLKS(CLK_MOVSX_R_R);
						src = genregb(modrm & 7);
					} else {
						CLKS(CLK_MOVSX_R_MEM);
						src = mem->read8(modrm_seg_ea(modrm));
					}
					genregw(modrm >> 3 & 7) = ((src & 0x80)?0xff00:0x0000) | src;
				} else {
					if ((modrm & 0xc0) == 0xc0) {
						CLKS(CLK_MOVSX_R_R);
						src = genregb(modrm & 7);
					} else {
						CLKS(CLK_MOVSX_R_MEM);
						src = mem->read8(modrm_seg_ea(modrm));
					}
					genregd(modrm >> 3 & 7) = ((src & 0x80)?0xffffff00:0x0000000) | src;
				}
				break;
			case 0xbf: // MOVSX r32,r/m16
				modrm = mem->read8(get_seg_adr(CS, eip + 1));
				DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
				eip++;
				DAS_pr("MOVSX ");
				// xxx 現状 MOVSX ESP, EAXの様にsrcが
				// 32bit表記になる
				DAS_modrm(modrm, true, true, dword);
				eip++;
				if ((modrm & 0xc0) == 0xc0) {
					CLKS(CLK_MOVSX_R_R);
					src = genregw(modrm & 7);
				} else {
					CLKS(CLK_MOVSX_R_MEM);
					src = mem->read16(modrm_seg_ea(modrm));
				}
				genregd(modrm >> 3 & 7) = ((src & 0x8000)?0xffff0000:0x0000000) | src;
				break;

			default:
				DAS_pr("xxxxx\n");
				// LFS/LGS/LSS... (80386)
			}
			break;

/******************** Jcc ********************/

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
			JCC(JBE/JNA, flag8 & CF || flag8 & ZF);
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

/******************** ADD/OR/ADC/SBB/AND/SUB/XOR/CMP ********************/
/*
  76  543 210
  +--------+-----------+---------+---------+--------+---------------+
  |100000sw|mod ??? r/m|(DISP-LO)|(DISP-HI)|  data  |(data if sw=01)|
  +--------+-----------+---------+---------+--------+---------------+
  ???(ここではregではなく、opの拡張。これにより以下の様に命令が変わる):
  000:ADD, 001:OR, 010:ADC, 011:SBB, 100:AND, 101:SUB, 110:XOR, 111:CMP
*/
		case 0x80:
			// go through
		case 0x82:
			modrm = mem->read8(get_seg_adr(CS, eip));
			subop = modrm >> 3 & 7;
			DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
			DAS_pr("%s ", str8x[subop]);
			DAS_modrm(modrm, false, false, byte);
			DAS_pr("0x%02x\n", mem->read8(get_seg_adr(CS, eip + nr_disp_modrm(modrm) + 1)));

			switch (subop) {
			case 0: // ADD r/m8, imm8
				CAL_RM_IM(b, b, ADD, u8, 0);
				break;
			case 1: // OR r/m8, imm8
				LOGOP_RM_IM(b, b, |);
				break;
			case 2: // ADC r/m8, imm8
				CAL_RM_IM(b, b, ADC, u8, (flag8 & CF));
				break;
			case 3: // SBB r/m8, imm8
				CAL_RM_IM(b, b, SBB, u8, -(flag8 & CF));
				break;
			case 4: // AND r/m8, imm8
				LOGOP_RM_IM(b, b, &);
				break;
			case 5: // SUB r/m8, imm8
				CAL_RM_IM(b, b, SUB, u8, 0);
				break;
			case 6: // XOR r/m8, imm8
				LOGOP_RM_IM(b, b, ^);
				break;
			case 7: // CMP r/m8, imm8
				CMP_RM_IM(b, b);
				break;
			}
			break;

		case 0x81:
			modrm = mem->read8(get_seg_adr(CS, eip));
			subop = modrm >> 3 & 7;
			DAS_prt_post_op(nr_disp_modrm(modrm) + (opsize == size16)?3:5);
			DAS_pr("%s ", str8x[subop]);
			DAS_modrm(modrm, false, false, word);
			DAS_pr((opsize == size16)?"0x%04x\n":"0x%08x\n", (opsize == size16)?mem->read16(get_seg_adr(CS, eip + 1)):mem->read32(get_seg_adr(CS, eip + 1)));

			switch (subop) {

			case 0: // ADD r/m16, imm16 (ADD r/m32, imm32)
				if (opsize == size16) {
					CAL_RM_IM(w, w, ADD, u16, 0);
				} else {
					CAL_RM_IM(d, d, ADD, u32, 0);
				}
				break;
			case 1: // OR r/m16, imm16 (OR r/m32, imm32)
				if (opsize == size16) {
					LOGOP_RM_IM(w, w, |);
				} else {
					LOGOP_RM_IM(d, d, |);
				}
				break;
			case 2: // ADC r/m16, imm16 (ADC r/m32, imm32)
				if (opsize == size16) {
					CAL_RM_IM(w, w, ADC, u16, (flag8 & CF));
				} else {
					CAL_RM_IM(d, d, ADC, u32, (flag8 & CF));
				}
				break;
			case 3: // SBB r/m16, imm16 (SBB r/m32, imm32)
				if (opsize == size16) {
					CAL_RM_IM(w, w, SBB, u16, -(flag8 & CF));
				} else {
					CAL_RM_IM(d, d, SBB, u32, -(flag8 & CF));
				}
				break;
			case 4: // AND r/m16, imm16 (AND r/m32, imm32)
				if (opsize == size16) {
					LOGOP_RM_IM(w, w, &);
				} else {
					LOGOP_RM_IM(d, d, &);
				}
				break;
			case 5: // SUB r/m16, imm16 (SUB r/m32, imm32)
				if (opsize == size16) {
					CAL_RM_IM(w, w, SUB, u16, 0);
				} else {
					CAL_RM_IM(d, d, SUB, u32, 0);
				}
				break;

			case 6: // XOR r/m16, imm16 (XOR r/m32, imm32)
				if (opsize == size16) {
					LOGOP_RM_IM(w, w, ^);
				} else {
					LOGOP_RM_IM(d, d, ^);
				}
				break;
			case 7: // CMP r/m16, imm16 (CMP r/m32, im32)
				if (opsize == size16) {
					CMP_RM_IM(w, w);
				} else {
					CMP_RM_IM(d, d);
				}
				break;
			}
			break;

		// ADD/ADC/AND/SUB/SBB/CMP r/m16, imm8 (... r/m32, imm8)
		case 0x83:
			//w-bit 1なのでワード動作、s-bit 0なので即値は byte
			modrm = mem->read8(get_seg_adr(CS, eip));
			subop = modrm >> 3 & 7;
			DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
			DAS_pr("%s ", str8x[subop]);
			// xxx size32を要考慮
			DAS_modrm(modrm, false, false, word);
			DAS_pr("0x%02x\n", mem->read8(get_seg_adr(CS, eip + 1)));

			switch (subop) {
			case 0: // ADD r/m16, imm8 (ADD r/m32, imm8)
				if (opsize == size16) {
					CAL_RM_IM(w, b, ADD, u16, 0);
				} else {
					CAL_RM_IM(d, b, ADD, u32, 0);
				}
				break;
			case 1: // OR r/m16, imm8 (OR r/m32, imm8)
				if (opsize == size16) {
					LOGOP_RM_IM(w, b, |);
				} else {
					LOGOP_RM_IM(d, b, |);
				}
				break;
			case 2: // ADC r/m16, imm8 (ADC r/m32, imm8)
				if (opsize == size16) {
					CAL_RM_IM(w, b, ADC, u16, (flag8 & CF));
				} else {
					CAL_RM_IM(d, b, ADC, u32, (flag8 & CF));
				}
				break;
			case 3: // SBB r/m16, imm8 (SBB r/m32, imm8)
				if (opsize == size16) {
					CAL_RM_IM(w, b, SBB, u16, -(flag8 & CF));
				} else {
					CAL_RM_IM(d, b, SBB, u32, -(flag8 & CF));
				}
				break;
			case 4: // AND r/m16, imm8 (AND r/m32, imm8)
				if (opsize == size16) {
					LOGOP_RM_IM(w, b, &);
				} else {
					LOGOP_RM_IM(d, b, &);
				}
				break;
			case 5: // SUB r/m16, imm8 (SUB r/m32, imm8)
				if (opsize == size16) {
					CAL_RM_IM(w, b, SUB, u16, 0);
				} else {
					CAL_RM_IM(d, b, SUB, u32, 0);
				}
				break;
			case 6: // XOR r/m16, imm8 (XOR r/m32, imm8)
				if (opsize == size16) {
					LOGOP_RM_IM(w, b, ^);
				} else {
					LOGOP_RM_IM(d, b, ^);
				}
				break;
			case 7: // CMP r/m16, imm8 (CMP r/m32, imm8)
				if (opsize == size16) {
					CMP_RM_IM(w, b);
				} else {
					CMP_RM_IM(d, b);
				}
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
		case 0x86: // XCHG r8, r/m8 or XCHG r/m8, r8
			XCHG_R_RM(b);
			break;
		case 0x87: // XCHG r16, r/m16 or XCHG r/m16, r16 (XCHG r32, r/m32 or XCHG r/m32, r32)
			if (opsize == size16) {
				XCHG_R_RM(w);
			} else {
				XCHG_R_RM(d);
			}
			break;

#define XCHG_GENREGW(reg)				\
			DAS_prt_post_op(0);		\
			DAS_pr("XCHG AX, "#reg"\n");	\
			dst = ax;			\
			ax = reg;			\
			reg = dst;

#define XCHG_GENREGD(reg)				\
			DAS_prt_post_op(0);		\
			DAS_pr("XCHG EAX, "#reg"\n");	\
			dst = eax;			\
			eax = reg;			\
			reg = dst;

		case 0x90: // XCHG AX (XCHG EAX) -> NOP
			CLKS(CLK_NOP);
			if (opsize == size16) {
				XCHG_GENREGW(ax);
			} else {
				XCHG_GENREGW(eax);
			}
			break;
		case 0x91: // XCHG CX (XCHG ECX)
			CLKS(CLK_XCHG_R_R);
			if (opsize == size16) {
				XCHG_GENREGW(cx);
			} else {
				XCHG_GENREGW(ecx);
			}
			break;
		case 0x92: // XCHG DX (XCHG EDX)
			CLKS(CLK_XCHG_R_R);
			if (opsize == size16) {
				XCHG_GENREGW(dx);
			} else {
				XCHG_GENREGW(edx);
			}
			break;
		case 0x93: // XCHG BX (XCHG EBX)
			CLKS(CLK_XCHG_R_R);
			if (opsize == size16) {
				XCHG_GENREGW(bx);
			} else {
				XCHG_GENREGW(ebx);
			}
			break;
		case 0x94: // XCHG SP (XCHG ESP)
			CLKS(CLK_XCHG_R_R);
			if (opsize == size16) {
				XCHG_GENREGW(sp);
			} else {
				XCHG_GENREGW(esp);
			}
			break;
		case 0x95: // XCHG BP (XCHG EBP)
			CLKS(CLK_XCHG_R_R);
			if (opsize == size16) {
				XCHG_GENREGW(bp);
			} else {
				XCHG_GENREGW(ebp);
			}
			break;
		case 0x96: // XCHG SI (XCHG ESI)
			CLKS(CLK_XCHG_R_R);
			if (opsize == size16) {
				XCHG_GENREGW(si);
			} else {
				XCHG_GENREGW(esi);
			}
			break;
		case 0x97: // XCHG DI (XCHG EDI)
			CLKS(CLK_XCHG_R_R);
			if (opsize == size16) {
				XCHG_GENREGW(di);
			} else {
				XCHG_GENREGW(edi);
			}
			break;

/******************** MOV ********************/
/*
  76  543 210
  +--------+-----------+---------+---------+
  |100010dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
  +--------+-----------+---------+---------+
*/
		case 0x88: // MOV r/m8, r8
			CLKS(CLK_MOV_RM_R);
			modrm = mem->read8(get_seg_adr(CS, eip));
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
			CLKS(CLK_MOV_RM_R);
			modrm = mem->read8(get_seg_adr(CS, eip));
			DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
			DAS_pr("MOV ");
			DAS_modrm(modrm, true, false, word);
			eip++;
			if (opsize == size16) {
				if ((modrm & 0xc0) == 0xc0) {
					genregw(modrm & 0x07) = genregw(modrm >> 3 & 7);
				} else {
					mem->write16(modrm_seg_ea(modrm), genregw(modrm >> 3 & 7));
				}
			} else {
				if ((modrm & 0xc0) == 0xc0) {
					genregd(modrm & 0x07) = genregd(modrm >> 3 & 7);
				} else {
					mem->write32(modrm_seg_ea(modrm), genregd(modrm >> 3 & 7));
				}
			}
			break;

		case 0x8a: // MOV r8, r/m8
			modrm = mem->read8(get_seg_adr(CS, eip));
			DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
			DAS_pr("MOV ");
			DAS_modrm(modrm, true, true, byte);
			eip++;
			if ((modrm & 0xc0) == 0xc0) {
				CLKS(CLK_MOV_R_R);
				genregb(modrm >> 3 & 7) = genregb(modrm & 7);
			} else {
				CLKS(CLK_MOV_R_MEM);
				genregb(modrm >> 3 & 7) = mem->read8(modrm_seg_ea(modrm));
			}
			break;

		case 0x8b: // MOV r16, r/m16 (MOV r32, r/m32)
			modrm = mem->read8(get_seg_adr(CS, eip));
			DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
			DAS_pr("MOV ");
			DAS_modrm(modrm, true, true, word);
			eip++;
			if (opsize == size16) {
				if ((modrm & 0xc0) == 0xc0) {
					CLKS(CLK_MOV_R_R);
					genregw(modrm >> 3 & 7) = genregw(modrm & 7);
				} else {
					CLKS(CLK_MOV_R_MEM);
					genregw(modrm >> 3 & 7) =
						mem->read16(modrm_seg_ea(modrm));
				}
			} else {
				if ((modrm & 0xc0) == 0xc0) {
					CLKS(CLK_MOV_R_R);
					genregd(modrm >> 3 & 7) = genregd(modrm & 7);
				} else {
					CLKS(CLK_MOV_R_MEM);
					genregd(modrm >> 3 & 7) =
						mem->read32(modrm_seg_ea(modrm));
				}
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
			CLKS(CLK_MOV_RM_SR);
			modrm = mem->read8(get_seg_adr(CS, eip));
			sreg = modrm >> 3 & 3;
			DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
			DAS_pr("MOV ");
			DAS_modrm(modrm, false, false, word);
			DAS_pr("%s\n", segreg_name[sreg]);
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
			CLKS(CLK_LEA);
			modrm = mem->read8(get_seg_adr(CS, eip));
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
			modrm = mem->read8(get_seg_adr(CS, eip));
			sreg = modrm >> 3 & 7;
			DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
			DAS_pr("MOV %s, ", segreg_name[sreg]);
			DAS_modrm(modrm, false, true, word);
			eip++;
			// セグメントはbaseも更新
			if ((modrm & 0xc0) == 0xc0) {
				CLKS(isRealMode?CLK_MOV_SR_R:CLK_PM_MOV_SR_R);
				update_segreg(sreg, genregw(modrm & 7));
			} else {
				CLKS(isRealMode?CLK_MOV_SR_MEM:CLK_PM_MOV_SR_MEM);
				update_segreg(sreg, mem->read16(modrm_seg_ea(modrm)));
			}
			break;

/*
  +---------+--------+--------+
  |1010 000w|addr-lo |addr-hi |
  +---------+--------+--------+
*/
		case 0xa0: // MOV AL, moffs8
			CLKS(CLK_MOV_MOF);
			DAS_prt_post_op(2);
			src = mem->read16(get_seg_adr(CS, eip));
			DAS_pr("MOV AL, byte ptr [0x%04x]\n", src);
			al = mem->read8(get_seg_adr(DS, src));
			eip += 2;
			break;
		case 0xa1: // MOV AX, moffs16 (MOV EAX moffs32)
			CLKS(CLK_MOV_MOF);
			if (opsize == size16) {
				DAS_prt_post_op(2);
				src = mem->read16(get_seg_adr(CS, eip));
				DAS_pr("MOV AX, word ptr [0x%04x]\n", src);
				ax = mem->read16(get_seg_adr(DS, src));
				eip += 2;
			} else {
				DAS_prt_post_op(4);
				src = mem->read32(get_seg_adr(CS, eip));
				DAS_pr("MOV EAX, word ptr [0x%08x]\n", src);
				eax = mem->read32(get_seg_adr(DS, src));
				eip += 4;
			}
			break;
		case 0xa2: // MOV moffs8, AL
			CLKS(CLK_MOV_MOF);
			DAS_prt_post_op(2);
			src = mem->read16(get_seg_adr(CS, eip));
			DAS_pr("MOV AL, byte ptr [0x%04x]\n", src);
			mem->write8(get_seg_adr(DS, src), al);
			eip += 2;
			break;
		case 0xa3: // MOV moffs16, AX (MOV moffs32, EAX)
			CLKS(CLK_MOV_MOF);
			if (opsize == size16) {
				DAS_prt_post_op(2);
				src = mem->read16(get_seg_adr(CS, eip));
				DAS_pr("MOV AX, word ptr [0x%04x]\n", src);
				mem->write16(get_seg_adr(DS, src), ax);
				eip += 2;
			} else {
				DAS_prt_post_op(4);
				src = mem->read32(get_seg_adr(CS, eip));
				DAS_pr("MOV EAX, word ptr [0x%08x]\n", src);
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
			CLKS(CLK_MOV_R_IMM);
			DAS_prt_post_op(1);
			DAS_pr("MOV %s, 0x%02x\n", genreg_name[0][op & 7], mem->read8(get_seg_adr(CS, eip)));
			*genregb[op & 7] = mem->read8(get_seg_adr(CS, eip++));
			break;
		case 0xb8: // MOV AX, imm16 (MOV EAX, imm32)
			// go through
		case 0xb9: // MOV CX, imm16 (MOV ECX, imm32)
			// go through
		case 0xba: // MOV DX, imm16 (MOV EDX, imm32)
			// go through
		case 0xbb: // MOV BX, imm16 (MOV EBX, imm32)
			// go through
		case 0xbc: // MOV SP, imm16 (MOV ESP, imm32)
			// go through
		case 0xbd: // MOV BP, imm16 (MOV EBP, imm32)
			// go through
		case 0xbe: // MOV SI, imm16 (MOV ESI, imm32)
			// go through
		case 0xbf: // MOV DI, imm16 (MOV EDI, imm32)
			CLKS(CLK_MOV_R_IMM);
			if (opsize == size16) {
				DAS_prt_post_op(2);
				DAS_pr("MOV %s, 0x%04x\n", genreg_name[1][op & 7], mem->read16(get_seg_adr(CS, eip)));
				genregw(op & 7) = mem->read16(get_seg_adr(CS, eip));
				eip += 2;
			} else {
				DAS_prt_post_op(4);
				DAS_pr("MOV %s, 0x%08x\n", genreg_name[2][op & 7], mem->read32(get_seg_adr(CS, eip)));
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
			CLKS(CLK_MOV_R_IMM);
			modrm = mem->read8(get_seg_adr(CS, eip));
			DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
			DAS_pr("MOV ");
			DAS_modrm(modrm, false, false, byte);
			DAS_pr("0x%02x\n", mem->read8(get_seg_adr(CS, eip + nr_disp_modrm(modrm) + 1)));
			eip++;
			if ((modrm & 0xc0) == 0xc0) {
				genregb(modrm & 7) = mem->read8(get_seg_adr(CS, eip));
			} else {
				// modrm_seg_ea()内でeipが更新されるので注意
				tmpadr = modrm_seg_ea(modrm);
				mem->write8(tmpadr, mem->read8(get_seg_adr(CS, eip)));
			}
			eip++;
			break;
		case 0xc7: // MOV r/m16, imm16 (MOV r/m32, imm32)
			CLKS(CLK_MOV_R_IMM);
			if (opsize == size16) {
				modrm = mem->read8(get_seg_adr(CS, eip));
				DAS_prt_post_op(nr_disp_modrm(modrm) + 3);
				DAS_pr("MOV ");
				DAS_modrm(modrm, false, false, word);
				DAS_pr("0x%04x\n", mem->read16(get_seg_adr(CS, eip + nr_disp_modrm(modrm) + 1)));
				eip++;
				if ((modrm & 0xc0) == 0xc0) {
					genregw(modrm & 7) = mem->read16(get_seg_adr(CS, eip));
				} else {
					tmpadr = modrm_seg_ea(modrm);
					mem->write16(tmpadr, mem->read16(get_seg_adr(CS, eip)));
				}
				eip += 2;
			} else {
				modrm = mem->read8(get_seg_adr(CS, eip));
				DAS_prt_post_op(nr_disp_modrm(modrm) + 5);
				DAS_pr("MOV ");
				DAS_modrm(modrm, false, false, dword);
				DAS_pr("0x%08x\n", mem->read32(get_seg_adr(CS, eip + nr_disp_modrm(modrm) + 1)));
				eip++;
				if ((modrm & 0xc0) == 0xc0) {
					genregd(modrm & 7) = mem->read32(get_seg_adr(CS, eip));
				} else {
					tmpadr = modrm_seg_ea(modrm);
					mem->write32(tmpadr, mem->read32(get_seg_adr(CS, eip)));
				}
				eip += 4;
			}
			break;

/******************** TEST ********************/
// OF/CF:0, SF/ZF/PF:結果による, AF:未定義

		case 0x84: // TEST r/m8, r8
			TEST_RM_R(b);
			break;
		case 0x85: // TEST r/m16, r16 (TEST rm/32, r32)
			if (opsize == size16) {
				TEST_RM_R(w);
			} else {
				TEST_RM_R(d);
			}
			break;

		case 0xa8: // test al, imm8
			CLKS(CLK_TEST_R_IMM);
			DAS_prt_post_op(1);
			DAS_pr("TEST AL, 0x%02x\n", mem->read8(get_seg_adr(CS, eip)));
			dst = al & mem->read8(get_seg_adr(CS, eip++));
			FLAG_LOGOPb(dst);
			break;
		case 0xa9: // test ax, imm16 (test eax, imm32)
			CLKS(CLK_TEST_R_IMM);
			if (opsize == size16) {
				DAS_prt_post_op(2);
				DAS_pr("TEST AX, 0x%04x\n", mem->read16(get_seg_adr(CS, eip)));
				dst = ax & mem->read16(get_seg_adr(CS, eip));
				eip += 2;
				FLAG_LOGOPw(dst);
			} else {
				DAS_prt_post_op(4);
				DAS_pr("TEST EAX, 0x%08x\n", mem->read32(get_seg_adr(CS, eip)));
				dst = eax & mem->read32(get_seg_adr(CS, eip));
				eip += 4;
				FLAG_LOGOPd(dst);
			}
			break;

/******************** CBW/CWD/CDQ ********************/

		case 0x98: // CBW
			CLKS(CLK_CBW);
			DAS_prt_post_op(0);
			DAS_pr("CBW\n");
			ah = (al & 0x80)? 0xff : 0x0 ;
			break;
		case 0x99: // CWD (CDQ)
			CLKS(CLK_CWD);
			DAS_prt_post_op(0);
			if (opsize == size16) {
				DAS_pr("CWD\n");
				if (ax & 0x8000) {
					dx = 0xffff;
				} else {
					dx = 0;
				}
			} else {
				DAS_pr("CDQ\n");
				if (eax & 0x80000000) {
					edx = 0xffffffff;
				} else {
					edx = 0;
				}
			}
			break;

/******************** WAIT ********************/

		case 0x9b:
			DAS_prt_post_op(0);
			DAS_pr("WAIT\n");
			// コプロ未実装なのでなにもしない
			break;

/******************** SAHF/LAHF ********************/

		case 0x9e:
			CLKS(CLK_SAHF);
			DAS_prt_post_op(0);
			DAS_pr("SAHF\n");
			flag8 = 0x2; // xxx eflagsのbit 1は常に1らしい
			flag8 |= ah;
			break;
		case 0x9f:
			CLKS(CLK_LAHF);
			DAS_prt_post_op(0);
			DAS_pr("LAHF\n");
			ah = flag8;
			break;


/******************** MOVS ********************/

		case 0xa4: // MOVS m8, m8
			DAS_prt_post_op(0);
			DAS_pr("MOVSB\n");
			if (opsize == size16) {
				(repe_prefix)? cnt = cx, cx = 0 : cnt = 1;
				while (cnt != 0) {
					mem->write8(get_seg_adr(ES, di), mem->read8(get_seg_adr(DS, si)));
					di++;
					si++;
					cnt--;
					CLKS(CLK_MOVS);
					if (clks <= 0 && cnt != 0) {
						cx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
			} else { // 8bit処理でもopsize32用の対応が必要
				(repe_prefix)? cnt = ecx, ecx = 0 : cnt = 1;
				while (cnt != 0) {
					mem->write8(get_seg_adr(ES, edi), mem->read8(get_seg_adr(DS, esi)));
					edi++;
					esi++;
					cnt--;
					CLKS(CLK_MOVS);
					if (clks <= 0 && cnt != 0) {
						ecx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
			}
			break;
		case 0xa5: // MOVS m16, m16 (MOVS m32, m32)
			DAS_prt_post_op(0);
			if (opsize == size16) {
				DAS_pr("MOVSW\n");
				(repe_prefix)? cnt = cx, cx = 0 : cnt = 1;
				while (cnt != 0) {
					mem->write16(get_seg_adr(ES, di), mem->read16(get_seg_adr(DS, si)));
					di += 2;
					si += 2;
					cnt--;
					CLKS(CLK_MOVS);
					if (clks <= 0 && cnt != 0) {
						cx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
			} else {
				DAS_pr("MOVSD\n");
				(repe_prefix)? cnt = ecx, ecx = 0 : cnt = 1;
				while (cnt != 0) {
					mem->write32(get_seg_adr(ES, edi), mem->read32(get_seg_adr(DS, esi)));
					edi++;
					esi++;
					cnt--;
					CLKS(CLK_MOVS);
					if (clks <= 0 && cnt != 0) {
						ecx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
			}
			break;

/******************** CMPS ********************/

		case 0xa6:
			DAS_prt_post_op(0);
			DAS_pr("CMPSB\n");
			if (opsize == size16) {
				(repne_prefix)?	cnt = cx : cnt = 1;
				incdec = (flagu8 & DF8)? -1 : +1;
				while (cnt != 0) {
					CLKS(CLK_CMPS);
					dst = mem->read8(get_seg_adr(DS, si));
					src = mem->read8(get_seg_adr(ES, di));
					res = dst - src;
					if (res == 0) { // ZF==1
						break;
					}
					si += incdec;
					di += incdec;
					cnt--;
					if (clks <= 0 && cnt != 0) {
						cx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
				cx = cnt;
				FLAG8bSUB(res, src, dst, );
				OF_SUBb(res, src, dst);
			} else { // 8bit処理でもopsize32用の対応が必要
				(repne_prefix)?	cnt = ecx : cnt = 1;
				incdec = (flagu8 & DF8)? -1 : +1;
				while (cnt != 0) {
					CLKS(CLK_CMPS);
					dst = mem->read8(get_seg_adr(DS, esi));
					src = mem->read8(get_seg_adr(ES, edi));
					res = dst - src;
					if (res == 0) { // ZF==1
						break;
					}
					esi += incdec;
					edi += incdec;
					cnt--;
					if (clks <= 0 && cnt != 0) {
						ecx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
				ecx = cnt;
				FLAG8bSUB(res, src, dst, );
				OF_SUBb(res, src, dst);
			}
			break;
		case 0xa7: // CMPSW (CMPSD)
			DAS_prt_post_op(0);
			if (opsize == size16) {
				DAS_pr("CMPSW\n");
				(repne_prefix)?	cnt = cx : cnt = 1;
				incdec = (flagu8 & DF8)? -2 : +2;
				while (cnt != 0) {
					CLKS(CLK_CMPS);
					dst = mem->read16(get_seg_adr(DS, si));
					src = mem->read16(get_seg_adr(ES, di));
					res = dst - src;
					if (res == 0) { // ZF==1
						break;
					}
					si += incdec;
					di += incdec;
					cnt--;
					if (clks <= 0 && cnt != 0) {
						cx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
				cx = cnt;
				FLAG8wSUB(res, src, dst, );
				OF_SUBw(res, src, dst);
			} else {
				DAS_pr("CMPSD\n");
				(repne_prefix)?	cnt = ecx : cnt = 1;
				incdec = (flagu8 & DF8)? -4 : +4;
				while (cnt != 0) {
					CLKS(CLK_CMPS);
					dst = mem->read32(get_seg_adr(DS, esi));
					src = mem->read32(get_seg_adr(ES, edi));
					res = dst - src;
					if (res == 0) { // ZF==1
						break;
					}
					esi += incdec;
					edi += incdec;
					cnt--;
					if (clks <= 0 && cnt != 0) {
						ecx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
				ecx = cnt;
				FLAG8dSUB(res, src, dst, );
				OF_SUBd(res, src, dst);
			}
			break;

/******************** STOS ********************/

		case 0xaa:
			DAS_prt_post_op(0);
			DAS_pr("STOSB\n");
			if (opsize == size16) {
				(repe_prefix)? cnt = cx, cx = 0 : cnt = 1;
				// whileの中で毎回ifするのは無駄なので
				// あらかじめ加減値を算出する
				incdec = (flagu8 & DF8)? -1 : +1;
				while (cnt != 0) {
					mem->write8(get_seg_adr(ES, di), al);
					di += incdec;
					cnt--;
					CLKS(CLK_STOS);
					if (clks <= 0 && cnt != 0) {
						cx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
			} else { // 8bit処理でもopsize32用の対応が必要
				(repe_prefix)? cnt = ecx, ecx = 0 : cnt = 1;
				incdec = (flagu8 & DF8)? -1 : +1;
				while (cnt != 0) {
					mem->write8(get_seg_adr(ES, edi), al);
					edi += incdec;
					cnt--;
					CLKS(CLK_STOS);
					if (clks <= 0 && cnt != 0) {
						ecx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
			}
			break;
		case 0xab:
			DAS_prt_post_op(0);
			if (opsize == size16) {
				DAS_pr("STOSW\n");
				(repe_prefix)? cnt = cx, cx = 0 : cnt = 1;
				incdec = (flagu8 & DF8)? -2 : +2;
				while (cnt != 0) {
					mem->write16(get_seg_adr(ES, di), ax);
					di += incdec;
					cnt--;
					CLKS(CLK_STOS);
					if (clks <= 0 && cnt != 0) {
						cx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
			} else {
				DAS_pr("STOSD\n");
				(repe_prefix)? cnt = ecx, ecx = 0 : cnt = 1;
				incdec = (flagu8 & DF8)? -4 : +4;
				while (cnt != 0) {
					mem->write32(get_seg_adr(ES, edi), eax);
					edi += incdec;
					cnt--;
					CLKS(CLK_STOS);
					if (clks <= 0 && cnt != 0) {
						ecx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
			}
			break;

/******************** LODS ********************/

		// xxx これにリピートプリフィックスつける意味あるのか？
		case 0xac:
			DAS_prt_post_op(0);
			DAS_pr("LODSB\n");
			if (opsize == size16) {
				(repe_prefix)? cnt = cx, cx = 0 : cnt = 1;
				while (cnt != 0) {
					al = mem->read8(get_seg_adr(DS, si));
					si++;
					cnt--;
					CLKS(CLK_LODS);
					if (clks <= 0 && cnt != 0) {
						cx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
			} else {
				(repe_prefix)? cnt = ecx, ecx = 0 : cnt = 1;
				while (cnt != 0) {
					al = mem->read8(get_seg_adr(DS, esi));
					esi++;
					cnt--;
					CLKS(CLK_LODS);
					if (clks <= 0 && cnt != 0) {
						ecx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
			}
			break;
		case 0xad: // LODSW (LODSD)
			DAS_prt_post_op(0);
			if (opsize == size16) {
				DAS_pr("LODSW\n");
				(repe_prefix)? cnt = cx, cx = 0 : cnt = 1;
				while (cnt != 0) {
					// xxx プロテクトモードで
					// オペレーションサイズオーバーライド
					// した時だけ上位16bitも参照する?
					// それともリアルモードでも参照する?
					ax = mem->read16(get_seg_adr(DS, esi));
					si += 2;
					cnt--;
					CLKS(CLK_LODS);
					if (clks <= 0 && cnt != 0) {
						cx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
			} else {
				DAS_pr("LODSD\n");
				(repe_prefix)? cnt = ecx, ecx = 0 : cnt = 1;
				while (cnt != 0) {
					eax = mem->read32(get_seg_adr(DS, esi));
					esi += 4;
					cnt--;
					CLKS(CLK_LODS);
					if (clks <= 0 && cnt != 0) {
						ecx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
			}
			break;

/******************** SCAS ********************/

		case 0xae:
			DAS_prt_post_op(0);
			DAS_pr("SCASB\n");
			if (opsize == size16) {
				(repe_prefix || repne_prefix)? cnt = cx : cnt = 1;
				incdec = (flagu8 & DF8)? -1 : +1;
				while (cnt != 0) {
					CLKS(CLK_SCAS);
					dst = al;
					src = mem->read8(get_seg_adr(ES, di));
					res = dst - src;
					// ZF==1
					if (repne_prefix && res == 0) {
						break;
					}
					// ZF==0
					if (repe_prefix && res != 0) {
						break;
					}
					di += incdec;
					cnt--;
					if (clks <= 0 && cnt != 0) {
						cx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
				cx = cnt;
			} else { // 8bit処理でもopsize32用の対応が必要
				(repe_prefix || repne_prefix)? cnt = ecx : cnt = 1;
				incdec = (flagu8 & DF8)? -1 : +1;
				while (cnt != 0) {
					CLKS(CLK_SCAS);
					dst = al;
					src = mem->read8(get_seg_adr(ES, edi));
					res = dst - src;
					// ZF==1
					if (repne_prefix && res == 0) {
						break;
					}
					// ZF==0
					if (repe_prefix && res != 0) {
						break;
					}
					edi += incdec;
					cnt--;
					if (clks <= 0 && cnt != 0) {
						ecx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
				ecx = cnt;
			}
			FLAG8bSUB(res, src, dst, );
			OF_SUBb(res, src, dst);
			break;
		case 0xaf: // SCASW (SCASD)
			DAS_prt_post_op(0);
			if (opsize == size16) {
				DAS_pr("SCASW\n");
				(repe_prefix || repne_prefix)? cnt = cx : cnt = 1;
				incdec = (flagu8 & DF8)? -2 : +2;
				while (cnt != 0) {
					CLKS(CLK_SCAS);
					dst = ax;
					src = mem->read16(get_seg_adr(ES, di));
					res = dst - src;
					// ZF==1
					if (repne_prefix && res == 0) {
						break;
					}
					// ZF==0
					if (repe_prefix && res != 0) {
						break;
					}
					di += incdec;
					cnt--;
					if (clks <= 0 && cnt != 0) {
						cx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
				cx = cnt;
				FLAG8wSUB(res, src, dst, );
				OF_SUBw(res, src, dst);
			} else {
				DAS_pr("SCASD\n");
				(repe_prefix || repne_prefix)? cnt = ecx : cnt = 1;
				incdec = (flagu8 & DF8)? -4 : +4;
				while (cnt != 0) {
					CLKS(CLK_SCAS);
					dst = eax;
					src = mem->read32(get_seg_adr(ES, edi));
					res = dst - src;
					// ZF==1
					if (repne_prefix && res == 0) {
						break;
					}
					// ZF==0
					if (repe_prefix && res != 0) {
						break;
					}
					edi += incdec;
					cnt--;
					if (clks <= 0 && cnt != 0) {
						ecx = cnt;
						isRealMode? ip-- : eip--;
						OP_CONTINUE();
						return clks;
					}
				}
				ecx = cnt;
				FLAG8dSUB(res, src, dst, );
				OF_SUBd(res, src, dst);
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
			DAS_pr("0x%02x\n", mem->read8(get_seg_adr(CS, eip + ndisp + 1)));
			src = mem->read8(get_seg_adr(CS, ndisp + eip + 1));
			src %= 8;
			switch (subop) {
			case 0x0: // ROL r/m8, imm8
				ROT_RM(ROL, b, L, src,
				       res = dst >> (8 - src) | dst << src);
				break;
			case 0x1: // ROR r/m8, imm8
				ROT_RM(ROR, b, R, src,
				       res = dst << (8 - src) | dst >> src);
				break;
			case 0x2: // RCL r/m8, imm8
				ROT_RM(RCL, b, L, src,
				       res = dst << src | (flag8 & CF) << (src - 1) | dst >> (8 - src + 1));
				break;
			case 0x3: // RCR r/m8, imm8
				ROT_RM(RCR, b, R, src,
				       res = dst >> src | (flag8 & CF) << (8 - src) | dst << (8 - src + 1));
				break;
			case 0x4: // SAL/SHL r/m8, imm8
				// go through
			case 0x6:
				SFT_SALSHL(b, src);
				break;
			case 0x5: // SHR r/m8, imm8
				SFT_SHR(b, src);
				break;
			case 0x7: // SAR r/m8, imm8
				SFT_SAR(b, src);
				break;
			}
			eip++;
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
			DAS_pr("0x%02x\n", mem->read8(get_seg_adr(CS, eip + ndisp + 1)));
			src = mem->read8(get_seg_adr(CS, ndisp + eip + 1));
			src = (opsize == size16)? src % 16 : src % 32;
			switch (subop) {
			case 0x0: // ROL r/m16, imm8 (ROL r/m32, imm8)
				if (opsize == size16) {
					ROT_RM(ROL, w, L, src,
					       res = dst >> (16 - src) | dst << src);
				} else {
					ROT_RM(ROL, d, L, src,
					       res = dst >> (32 - src) | dst << src);
				}
				break;
			case 0x1: // ROR r/m16, imm8
				if (opsize == size16) {
					ROT_RM(ROR, w, R, src,
					       res = dst << (16 - src) | dst >> src);
				} else {
					ROT_RM(ROR, d, R, src,
					       res = dst << (32 - src) | dst >> src);
				}
				break;
			case 0x2: // RCL r/m16, imm8
				if (opsize == size16) {
					ROT_RM(RCL, w, L, src,
					       res = dst << src | (flag8 & CF) << (src - 1) | dst >> (16 - src + 1));
				} else {
					ROT_RM(RCL, d, L, src,
					       res = dst << src | (flag8 & CF) << (src - 1) | dst >> (32 - src + 1));
				}
				break;
			case 0x3: // RCR r/m16, imm8
				if (opsize == size16) {
					ROT_RM(RCR, w, R, src,
					       res = dst >> src | (flag8 & CF) << (16 - src) | dst << (16 - src + 1));
				} else {
					ROT_RM(RCR, d, R, src,
					       res = dst >> src | (flag8 & CF) << (32 - src) | dst << (32 - src + 1));
				}
				break;
			case 0x4: // SAL/SHL r/m16, imm8
				// go through
			case 0x6:
				if (opsize == size16) {
					SFT_SALSHL(w, src);
				} else {
					SFT_SALSHL(d, src);
				}
				break;
			case 0x5: // SHR r/m16, imm8
				if (opsize == size16) {
					SFT_SHR(w, src);
				} else {
					SFT_SHR(d, src);
				}
				break;
			case 0x7: // SAR r/m16, imm8
				if (opsize == size16) {
					SFT_SAR(w, src);
				} else {
					SFT_SAR(d, src);
				}
				break;
			}
			eip++;
			break;

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
				ROT_RM(ROL, b, L, 1,
				       res = dst >> 7 | dst << 1);
				break;
			case 0x1: // ROR r/m8
				ROT_RM(ROR, b, R, 1,
				       res = dst << 7 | dst >> 1);
				break;
			case 0x2: // RCL r/m8
				ROT_RM(RCL, b, L, 1,
				       res = dst << 1 | (flag8 & CF));
				break;
			case 0x3: // RCR r/m8
				ROT_RM(RCR, b, R, 1,
				       res = dst >> 1 | (flag8 & CF) << 7);
				break;
			case 0x4: // SAL/SHL r/m8
				// go through
			case 0x6:
				SFT_SALSHL(b, 1);
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
			case 0x0: // ROL r/m16 (ROL r/m32)
				if (opsize == size16) {
					ROT_RM(ROL, w, L, 1,
					       res = dst >> 15 | dst << 1);
				} else {
					ROT_RM(ROL, d, L, 1,
					       res = dst >> 31 | dst << 1);
				}
				break;
			case 0x1: // ROR r/m16
				if (opsize == size16) {
					ROT_RM(ROR, w, R, 1,
					       res = dst << 15 | dst >> 1);
				} else {
					ROT_RM(ROR, d, R, 1,
					       res = dst << 31 | dst >> 1);
				}
				break;
			case 0x2: // RCL r/m16
				if (opsize == size16) {
					ROT_RM(RCL, w, L, 1,
					       res = dst << 1 | (flag8 & CF));
				} else {
					ROT_RM(RCL, d, L, 1,
					       res = dst << 1 | (flag8 & CF));
				}
				break;
			case 0x3: // RCR r/m16
				if (opsize == size16) {
					ROT_RM(RCR, w, R, 1,
					       res = dst >> 1 | (flag8 & CF) << 15);
				} else {
					ROT_RM(RCR, d, R, 1,
					       res = dst >> 1 | (flag8 & CF) << 31);
				}
				break;
			case 0x4: // SAL/SHL r/m16 (SAL/SHL r/m32)
				// go through
			case 0x6:
				if (opsize == size16) {
					SFT_SALSHL(w, 1);
				} else {
					SFT_SALSHL(d, 1);
				}
				break;
			case 0x5: // SHR r/m16
				if (opsize == size16) {
					SFT_SHR(w, 1);
				} else {
					SFT_SHR(d, 1);
				}
				break;
			case 0x7: // SAR r/m16
				if (opsize == size16) {
					SFT_SAR(w, 1);
				} else {
					SFT_SAR(d, 1);
				}
				break;
			}
			break;

		case 0xd2:
			modrm = mem->read8(get_seg_adr(CS, eip));
			subop = modrm >> 3 & 7;
			DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
			DAS_pr("%s ", strdx[subop]);
			DAS_modrm(modrm, false, false, byte);
			DAS_pr("CL\n");
			src = cl % 8;
			switch (subop) {
			case 0x0: // ROL r/m8, CL
				if (src != 0) {
					ROT_RM(ROL, b, L, src,
					       res = dst >> (8 - src) | dst << src);
				}
				break;
			case 0x1: // ROR r/m8, CL
				if (src != 0) {
					ROT_RM(ROR, b, R, src,
					       res = dst << (8 - src) | dst >> src);
				}
				break;
			case 0x2: // RCL r/m8, CL
				if (src != 0) {
					ROT_RM(RCL, b, L, src,
					       res = dst << src | (flag8 & CF) << (src - 1) | dst >> (8 - src + 1));
				}
				break;
			case 0x3: // RCR r/m8, CL
				if (src != 0) {
					ROT_RM(RCR, b, R, src,
					       res = dst >> src | (flag8 & CF) << (8 - src) | dst << (8 - src + 1));
				}
				break;
			case 0x4: // SAL/SHL r/m8, CL
				// go through
			case 0x6:
				SFT_SALSHL(b, cl);
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
			DAS_pr("CL\n");
			src = (opsize == size16)? cl % 16 : cl % 32;
			switch (subop) {
			case 0x0: // ROL r/m16, CL
				if (src != 0) {
					if (opsize == size16) {
						ROT_RM(ROL, w, L, src,
						       res = dst >> (16 - src) | dst << src);
					} else {
						ROT_RM(ROL, d, L, src,
						       res = dst >> (32 - src) | dst << src);
					}
				}
				break;
			case 0x1: // ROR r/m16, CL
				if (src != 0) {
					if (opsize == size16) {
						ROT_RM(ROR, w, R, src,
						       res = dst << (16 - src) | dst >> src);
					} else {
						ROT_RM(ROR, d, R, src,
						       res = dst << (32 - src) | dst >> src);
					}
				}
				break;
			case 0x2: // RCL r/m16, CL
				if (src != 0) {
					if (opsize == size16) {
						ROT_RM(RCL, w, L, src,
						       res = dst << src | (flag8 & CF) << (src - 1) | dst >> (16 - src + 1));
					} else {
						ROT_RM(RCL, d, L, src,
						       res = dst << src | (flag8 & CF) << (src - 1) | dst >> (32 - src + 1));
					}
				}
				break;
			case 0x3: // RCR r/m16, CL
				if (src != 0) {
					if (opsize == size16) {
						ROT_RM(RCR, w, R, src,
						       res = dst >> src | (flag8 & CF) << (16 - src) | dst << (16 - src + 1));
					} else {
						ROT_RM(RCR, d, R, src,
						       res = dst >> src | (flag8 & CF) << (32 - src) | dst << (32 - src + 1));
					}
				}
				break;
				// D3 /4 r/m16
			case 0x4: // SAL/SHL r/m16, CL (SAL/SHL r/m32, CL)
				// go through
			case 0x6:
				if (opsize == size16) {
					SFT_SALSHL(w, cl);
				} else {
					SFT_SALSHL(d, cl);
				}
				break;
			case 0x5: // SHR r/m16, CL
				if (opsize == size16) {
					SFT_SHR(w, cl);
				} else {
					SFT_SHR(d, cl);
				}
				break;
			case 0x7: // SAR r/m16, CL
				if (opsize == size16) {
					SFT_SAR(w, cl);
				} else {
					SFT_SAR(d, cl);
				}
				break;
			}
			break;

/******************** RET ********************/

// xxx プロテクトモードでcall/retに対して
// オペレーションサイズプリフィックスが使われることはないよね？

		case 0xc3: // RET  nearリターンする
			DAS_prt_post_op(0);
			DAS_pr("RET\n");
			POPW(ip);
			break;
		case 0xcb: // RET  farリターンする
			DAS_prt_post_op(0);
			DAS_pr("RET\n");
			POPW(ip);
			POPW(dst);
			update_segreg(CS, (u16)dst);
			break;
		case 0xc2: // RET  nearリターンする
			DAS_prt_post_op(1);
			// eipは後で書き換わるのであらかじめ取得しておく
			src = mem->read16(get_seg_adr(CS, eip));
			DAS_pr("RET 0x%04x\n", src);
			POPW(ip);
			sp += src;
			break;
		case 0xca: // RET  farリターンする
			DAS_prt_post_op(1);
			src = mem->read16(get_seg_adr(CS, eip));
			DAS_pr("RET 0x%04x\n", src);
			POPW(ip);
			POPW(dst);
			update_segreg(CS, (u16)dst);
			sp += src;
			break;

/******************** LES/LDS ********************/

		case 0xc4: // LES r16, m16:16 (LES r32, m16:32)
			CLKS(isRealMode?CLK_LES:CLK_PM_LES);
			LxS(LES, ES);
			break;
		case 0xc5: // LDS r16, m16:16 (LDS r32, m16:32)
			CLKS(isRealMode?CLK_LDS:CLK_PM_LDS);
			LxS(LDS, DS);
			break;

/******************** INT ********************/

		case 0xcc: // INT 3
			DAS_prt_post_op(0);
			DAS_pr("INT 3\n");
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
			DAS_pr("INT %d\n", tmpb);
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
			DAS_pr("INTO\n");
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
			CLKS(CLK_XLAT);
			DAS_prt_post_op(0);
			DAS_pr("XLAT\n");
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
				DAS_pr("FNINIT\n");
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
			DAS_pr("LOOPNE/LOOPNZ 0x%02x\n", tmpb);
			eip++; // jmpする前にインクリメントしておく必要がある
			cx--;
			if (cx != 0 && !(flag8 & ZF)) {
				eip += (s8)tmpb;
			}
			break;
		case 0xe1: // LOOPE/LOOPZ rel8
			DAS_prt_post_op(1);
			tmpb = mem->read8(get_seg_adr(CS, eip));
			DAS_pr("LOOPE/LOOPZ 0x%02x\n", tmpb);
			eip++;
			cx--;
			if (cx != 0 && flag8 & ZF) {
				eip += (s8)tmpb;
			}
			break;
		case 0xe2: // LOOP rel8
			DAS_prt_post_op(1);
			tmpb = mem->read8(get_seg_adr(CS, eip));
			DAS_pr("LOOP 0x%02x\n", tmpb);
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
			CLKS(isRealMode?CLK_IN:CLK_PM_IN);
			DAS_prt_post_op(1);
			DAS_pr("IN AL, 0x%02x\n", mem->read8(get_seg_adr(CS, eip)));
			al = io->read8(mem->read8(get_seg_adr(CS, eip++)));
			break;
		case 0xe5: // IN AX, imm8 (IN EAX, imm8)
			CLKS(isRealMode?CLK_IN:CLK_PM_IN);
			DAS_prt_post_op(1);
			if (opsize == size16) {
				DAS_pr("IN AX, 0x%02x\n",
				       mem->read8(get_seg_adr(CS, eip)));
				ax = io->read16(mem->read8(get_seg_adr(CS, eip++)));
			} else {
				DAS_pr("IN EAX, 0x%02x\n",
				       mem->read8(get_seg_adr(CS, eip)));
				eax = io->read32(mem->read8(get_seg_adr(CS, eip++)));
			}
			break;

/*
  +--------+--------+
  |1110011w| data-8 |
  +--------+--------+
*/
		case 0xe6: // OUT imm8, AL
			CLKS(isRealMode?CLK_OUT:CLK_PM_OUT);
			DAS_prt_post_op(1);
			DAS_pr("OUT 0x%02x, AL\n", mem->read8(get_seg_adr(CS, eip)));
			io->write8(mem->read8(get_seg_adr(CS, eip++)), al);
			break;
		case 0xe7: // OUT imm8, AX (OUT imm8, EAX)
			CLKS(isRealMode?CLK_OUT:CLK_PM_OUT);
			DAS_prt_post_op(1);
			if (opsize == size16) {
				DAS_pr("OUT 0x%02x, AX\n",
				       mem->read8(get_seg_adr(CS, eip)));
				io->write16(mem->read8(get_seg_adr(CS, eip++)), ax);
			} else {
				DAS_pr("OUT 0x%02x, EAX\n",
				       mem->read8(get_seg_adr(CS, eip)));
				io->write32(mem->read8(get_seg_adr(CS, eip++)), eax);
			}
			break;

/*
  +--------+
  |1110110w|
  +--------+
*/
		case 0xec: // IN AL, DX
			CLKS(isRealMode?CLK_IN_DX:CLK_PM_IN_DX);
			DAS_prt_post_op(0);
			DAS_pr("IN AL, DX\n");
			al = io->read8(dx);
			break;
		case 0xed: // IN AX, DX (IN EAX, DX)
			CLKS(isRealMode?CLK_IN_DX:CLK_PM_IN_DX);
			DAS_prt_post_op(0);
			if (opsize == size16) {
				DAS_pr("IN AX, DX\n");
				ax = io->read16(dx);
			} else {
				DAS_pr("IN EAX, DX\n");
				eax = io->read32(dx);
			}
			break;

/*
  +--------+
  |1110111w|
  +--------+
*/
		case 0xee: // OUT DX, AL
			CLKS(isRealMode?CLK_OUT_DX:CLK_PM_OUT_DX);
			DAS_prt_post_op(0);
			DAS_pr("OUT DX, AL\n");
			io->write8(dx, al);
			break;
		case 0xef: // OUT DX, AX (OUT DX, EAX)
			CLKS(isRealMode?CLK_OUT_DX:CLK_PM_OUT_DX);
			DAS_prt_post_op(0);
			if (opsize == size16) {
				DAS_pr("OUT DX, AX\n");
				io->write16(dx, ax);
			} else {
				DAS_pr("OUT DX, EAX\n");
				io->write32(dx, eax);
			}
			break;

/******************** CALL ********************/

		case 0xe8: // CALL rel16
			DAS_prt_post_op(2);
			warg1 = mem->read16(get_seg_adr(CS, eip));
			eip += 2;
			DAS_pr("CALL 0x%04x\n", warg1);
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
			DAS_pr("CALL %04x:%04x\n", warg2, warg1);
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
				DAS_pr("JMP 0x%04x\n", mem->read16(get_seg_adr(CS, eip)));
				eip += (s16)mem->read16(get_seg_adr(CS, eip)) + 2;
			} else {
				DAS_prt_post_op(4);
				DAS_pr("JMP 0x%08x\n", mem->read32(get_seg_adr(CS, eip)));
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
				DAS_pr("JMP %04x:%04x\n", warg2, warg1);
				update_segreg(CS, warg2);
				eip = warg1;
			} else{
				DAS_prt_post_op(6);
				darg1 = mem->read32(get_seg_adr(CS, eip));
				warg2 = mem->read16(get_seg_adr(CS, eip + 4));
				DAS_pr("JMP %04x:%08x\n", warg2, darg1);
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
			DAS_pr("JMP 0x%02x\n", mem->read8(get_seg_adr(CS, eip)));
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
				DAS_pr("0x%02x\n", mem->read8(get_seg_adr(CS, eip + nr_disp_modrm(modrm))));
				if ((modrm & 0xc0) == 0xc0) {
					CLKS(CLK_TEST_R_IMM);
					flag8 = flag_calb[genregb(modrm & 7) & mem->read8(get_seg_adr(CS, eip))];
				} else {
					CLKS(CLK_TEST_MEM_IMM);
					flag8 = flag_calb[mem->read8(modrm_seg_ea(modrm)) & mem->read8(get_seg_adr(CS, eip))];
				}
				flagu8 &= ~OFSET8;
				eip++;
				break;
			case 0x2: // NOT r/m8
				if ((modrm & 0xc0) == 0xc0) {
					CLKS(CLK_NOT_R);
					rm = modrm & 7;
					genregb(rm) = ~genregb(rm);
				} else {
					CLKS(CLK_NOT_MEM);
					tmpadr = modrm_seg_ea(modrm);
					mem->write8(tmpadr, ~mem->read8(tmpadr));
				}
				break;
			case 0x3: // NEG r/m8
				if ((modrm & 0xc0) == 0xc0) {
					CLKS(CLK_NEG_R);
					rm = modrm & 7;
					src = genregb(rm);
					res = 0 - src;
					genregb(rm) = (u8)res;
				} else {
					CLKS(CLK_NEG_MEM);
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
				if ((modrm & 0xc0) == 0xc0) {
					CLKS(CLK_DIV8_R);
					src = genregb(modrm & 7);
				} else {
					CLKS(CLK_DIV8_MEM);
					src = mem->read8(modrm_seg_ea(modrm));
				}
				al = dst / src;
				ah = dst % src;
				break;
			case 0x7: // IDIV r/m8
				dst = ax;
				if ((modrm & 0xc0) == 0xc0) {
					CLKS(CLK_IDIV8_R);
					src = genregb(modrm & 7);
				} else {
					CLKS(CLK_IDIV8_MEM);
					src = mem->read8(modrm_seg_ea(modrm));
				}
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
			// xxx 32bit時のダンプ表示数を未考慮
			DAS_modrm(modrm, false, (subop < 2)?false:true, word);
			eip++;
			switch (subop) {
			case 0x0: // TEST r/m16, imm16 (TEST r/m32, imm32)
				// go through
			case 0x1:
				DAS_pr("0x%04x\n", mem->read16(get_seg_adr(CS, eip + nr_disp_modrm(modrm))));
				if ((modrm & 0xc0) == 0xc0) {
					CLKS(CLK_TEST_R_IMM);
					flag8 = flag_calw[genregw(modrm & 7) & mem->read16(get_seg_adr(CS, eip))];
				} else {
					CLKS(CLK_TEST_MEM_IMM);
					flag8 = flag_calw[mem->read16(modrm_seg_ea(modrm)) & mem->read16(get_seg_adr(CS, eip))];
				}
				flagu8 &= ~OFSET8;
				eip += 2;
				break;
			case 0x2: // NOT r/m16 (xxx NOT r/m32)
				if ((modrm & 0xc0) == 0xc0) {
					CLKS(CLK_NOT_R);
					rm = modrm & 7;
					genregw(rm) = ~genregw(rm);
				} else {
					CLKS(CLK_NOT_MEM);
					tmpadr = modrm_seg_ea(modrm);
					mem->write16(tmpadr, ~mem->read16(tmpadr));
				}
				break;
			case 0x3: // NEG r/m16 (xxx NEG r/m32)
				if ((modrm & 0xc0) == 0xc0) {
					CLKS(CLK_NEG_R);
					rm = modrm & 7;
					src = genregw(rm);
					res = 0 - src;
					genregw(rm) = (u16)res;
				} else {
					CLKS(CLK_NEG_MEM);
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
			case 0x6: // DIV r/m16 (DIV r/m32)
				if (opsize == size16) {
					dst = (u32)(dx << 16) + ax;
					if ((modrm & 0xc0) == 0xc0) {
						CLKS(CLK_DIV16_R);
						src = genregw(modrm & 7);
					} else {
						CLKS(CLK_DIV16_MEM);
						src = mem->read16(modrm_seg_ea(modrm));
					}
					ax = dst / src;
					dx = dst % src;
				} else {
					dst64 = ((u64)edx << 32) + eax;
					if ((modrm & 0xc0) == 0xc0) {
						CLKS(CLK_DIV32_R);
						src = genregd(modrm & 7);
					} else {
						CLKS(CLK_DIV32_MEM);
						src = mem->read32(modrm_seg_ea(modrm));
					}
					eax = dst64 / src;
					edx = dst64 % src;
				}
				break;
			case 0x7: // IDIV r/m16 (IDIV r/m32)
				if (opsize == size16) {
					dst = (u32)(dx << 16) + ax;
					if ((modrm & 0xc0) == 0xc0) {
						CLKS(CLK_IDIV16_R);
						src = genregw(modrm & 7);
					} else {
						CLKS(CLK_IDIV16_MEM);
						src = mem->read16(modrm_seg_ea(modrm));
					}
					ax = (s32)dst / (s16)src;
					dx = (s32)dst % (s16)src;
				} else {
					dst64 = ((u64)edx << 32) + eax;
					if ((modrm & 0xc0) == 0xc0) {
						CLKS(CLK_IDIV32_R);
						src = genregd(modrm & 7);
					} else {
						CLKS(CLK_IDIV32_MEM);
						src = mem->read32(modrm_seg_ea(modrm));
					}
					eax = (s64)dst64 / (s32)src;
					edx = (s64)dst64 % (s32)src;
				}
				break;
			}
			break;

/******************** セグメントオーバーライド ********************/

		case 0x26: // SEG=ES
			SEG_OVRIDE(ES);
			return clks; // リターンする
		case 0x2e: // SEG=CS
			SEG_OVRIDE(CS);
			return clks; // リターンする
		case 0x36: // SEG=SS
			SEG_OVRIDE(SS);
			return clks; // リターンする
		case 0x3e: // SEG=DS
			SEG_OVRIDE(DS);
			return clks; // リターンする


/*************** オペランドサイズオーバーライドプリフィックス ***************/

		case 0x66:
			DAS_prt_post_op(0);
			DAS_pr("Ope Size Override\n");
			opsize_ovride = true;
			opsize = isRealMode?
				size32 : (sdcr[CS].attr & 0x400)?
				size16 : size32;
			return clks; // リターンする

/*************** アドレスサイズオーバーライドプリフィックス ***************/

		case 0x67:
			DAS_prt_post_op(0);
			DAS_pr("Addr Size Override\n");
			addrsize_ovride = true;
			addrsize = isRealMode?
				size32 : (sdcr[CS].attr & 0x400)?
				size16 : size32;
			return clks; // リターンする

/*************** LOCK ***************/

		case 0xf0:
			DAS_prt_post_op(0);
			DAS_pr("LOCK\n");
			// nothing to do
			break;

/*************** リピートプリフィックス ***************/

		case 0xf2:
			// repneでZFをチェックするのはCMPSとSCASのみ
			DAS_prt_post_op(0);
			DAS_pr("Repne Prefix\n");
			repne_prefix = true;
			return clks;
		case 0xf3:
			// repeでZFをチェックするのはCMPSとSCASのみ
			DAS_prt_post_op(0);
			DAS_pr("Repe Prefix\n");
			repe_prefix = true;
			return clks;

/******************** HLT ********************/

		case 0xf4:
			CLKS(CLK_HLT);
#ifdef CORE_DAS
			if (!DAS_hlt) {
				DAS_prt_post_op(0);
				DAS_pr("HLT\n");
			}
			DAS_hlt = true; // xxx いつかfalseに戻す
#endif
			eip--;
			return clks; // リターンする？

/******************** プロセッサコントロール ********************/

		case 0xf5:
			CLKS(CLK_CMC);
			DAS_prt_post_op(0);
			DAS_pr("CMC\n");
			(flag8 & CF)? flag8 &= ~CF : flag8 |= CF;
			break;
		case 0xf8:
			CLKS(CLK_CLC);
			DAS_prt_post_op(0);
			DAS_pr("CLC\n");
			flag8 &= ~CF;
			break;
		case 0xf9:
			CLKS(CLK_STC);
			DAS_prt_post_op(0);
			DAS_pr("STC\n");
			flag8 |= CF;
			break;
		case 0xfa:
			CLKS(CLK_CLI);
			DAS_prt_post_op(0);
			DAS_pr("CLI\n");
			flagu8 &= ~IFSET8;
			break;
		case 0xfb: // STI
			CLKS(CLK_STI);
			DAS_prt_post_op(0);
			DAS_pr("STI\n");
			flagu8 |= IFSET8;
			break;
		case 0xfc:
			CLKS(CLK_CLD);
			DAS_prt_post_op(0);
			DAS_pr("CLD\n");
			flagu8 &= ~DFSET8;
			break;
		case 0xfd:
			CLKS(CLK_STD);
			DAS_prt_post_op(0);
			DAS_pr("STD\n");
			flagu8 |= DFSET8;
			break;

/******************** INC/DEC/CALL/JMP/PUSH ********************/

		case 0xfe:
			modrm = mem->read8(get_seg_adr(CS, eip));
			subop = modrm >> 3 & 7;
			DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
			DAS_pr("%s ", strfe[subop]);
			DAS_modrm(modrm, false, true, byte);
			eip++;
			switch (subop) {
			case 0: // INC r/m8
				INCDEC_RM(b, +);
				break;
			case 1: // DEC r/m8
				INCDEC_RM(b, -);
				break;
			default:
				printf("xxxxx\n");
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
				INCDEC_RM(w, +);
				break;
			case 1: // DEC r/m16
				INCDEC_RM(w, -);
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
				CLKS(CLK_PUSH_MEM);
				if ((modrm & 0xc0) == 0xc0) {
					PUSHW_GENREG(genregw(modrm & 7));
				} else {
					PUSHW(mem->read16(modrm_seg_ea(modrm)));
				}
				break;
			default:
				printf("xxxxx\n");
			}
			break;

		default:
			DAS_prt_post_op(0);
			printf("xxxxxxxxxx\n");
		}

		if (seg_ovride > 0) {
			seg_ovride--;
			// オーバーライドしたセグメントを元に戻す
			if (seg_ovride == 0) {
				update_segreg(DS, segreg[DS]);
				update_segreg(SS, segreg[SS]);
			}
		}

		// {オペランド|アドレス}サイズオーバーライドプリフィックスを
		// 元に戻す
		opsize_ovride = false;
		addrsize_ovride = false;
		repne_prefix = false;
		repe_prefix = false;
		opsize = isRealMode?
			size16 : (sdcr[CS].attr & 0x400)?
			size32 : size16;
		addrsize = isRealMode?
			size16 : (sdcr[CS].attr & 0x400)?
			size32 : size16;
#ifdef CORE_DAS
		op_continue = false;
#endif
	}

	return clks;
}
