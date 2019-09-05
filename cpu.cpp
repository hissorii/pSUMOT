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

// - 引数aにセグメントを加算したアドレスを返却する
// - 386では、セグメント加算は、プロテクトモードでもリアルモードでも、
//   セグメントディスクリプターキャッシュ内のbase addressに対して行う
u32 CPU::get_seg_adr(const SEGREG seg, const u16 a) {
	return sdcr[seg].base + a;
}

void CPU::update_segreg(const u8 seg, const u16 n) {
	// リアルモードでは、SDCRのbaseを更新するだけ
	segreg[seg] = n;
	sdcr[seg].base = n << 4;
}

void CPU::reset() {
	opsize = size16;
	addrsize = size16;
	isRealMode = true;

	for (int i = 0; i < NR_SEGREG; i++) segreg[i] = 0x0000;
	segreg[CS] = 0xf000;
	ip = 0xfff0;
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
	printf("     eip:%08x", ip);
	printf("\n");
	for (i = 0; i < NR_SEGREG; i++) {
		printf("%s:%04x ", segreg_name[i], segreg[i]);
	}
	printf("\n\n");

        if (step == 120) {
                for (i = 0; i < 32; i++) {
                        printf("0x%02x ", mem->read8(0xf7fb0 + i));
                        if (((i + 1) % 16) == 0) printf("\n");
                }
                printf("\n");
        }
}

void CPU::DAS_prt_post_op(u8 n) {
	int i;
	for (i = 0; i < n; i++)
		printf(" %02x", mem->read8(get_seg_adr(CS, ip + i)));
	for (i = 0; i < 5 - n; i++)
		printf("%3c", ' ');
}

// リアルモード動作の場合
// isReg: mod reg R/M の reg が存在するか
// isDest: mod reg R/M の reg がDestinationになるか
// isWord: ワード転送かバイト転送か
void CPU::DAS_modrm16(u8 modrm, bool isReg, bool isDest, bool isWord) {
	u8 mod, reg, rm;
#define NR_RM 8
	char str[NR_RM][9] = {"[BX + SI", "[BX + DI", "[BP + SI", "[BP + DI", "[SI", "[DI", "[BP", "[BX", };
	char s[] = " + 0x????";

	if (isReg && isDest) {
		reg = modrm >> 3 & 7;
		printf("%s, ", genreg_name[isWord][reg]);
	}
	mod = modrm >> 6;
	rm = modrm & 7;

	if (mod == 3) {
		printf("%s%s", genreg_name[isWord][rm], isDest?"\n\n":", ");
		if (isReg && !isDest) {
			reg = modrm >> 3 & 7;
			printf("%s\n\n", genreg_name[isWord][reg]);
		}
		return;
	}

	printf("%s ptr ", isWord?"word":"byte");

	if (rm == 6 && mod == 0) {
		printf("[0x%04x]%s", mem->read16(get_seg_adr(CS, ip + 1)), isDest?"\n\n":", ");
		return;
	}

	if (mod == 1) {
		sprintf(s, " + 0x%02x", mem->read8(get_seg_adr(CS, ip + 1)));
	} else if (mod == 2) {
		sprintf(s, " + 0x%04x", mem->read16(get_seg_adr(CS, ip + 1)));
	} else {
		s[0] = '\0';
	}
	printf("%s%s]%s", str[rm], s, isDest?"\n\n":", ");

	if (isReg && !isDest) {
		reg = modrm >> 3 & 7;
		printf("%s\n\n", genreg_name[isWord][reg]);
	}
}

#define DAS_pr(...) printf(__VA_ARGS__)
#else
#define DAS_dump_reg()
#define DAS_prt_post_op(n)
#define DAS_modrm16(m, isR, isD, isW)
#define DAS_pr(...)
#endif // CORE_DAS

// modR/Mに続くディスプレースメントのバイト数を返す
u8 CPU::nr_disp_modrm(u8 modrm) {
	u8 mod, rm;

	mod = modrm >> 6;
	if (mod == 1)
		return 1;
	else if (mod == 2)
		return 2;
	else if (mod == 3)
		return 0;

	rm = modrm & 7;
	if (mod == 0 && rm == 6) return 2;

	return 0;
}

// modが11でないことはあらかじめチェックしておくこと
// Effective Addressを取得
// ipはModR/Mの次をポイントしていなければならない
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
			tmp16 = mem->read16(get_seg_adr(CS, ip));
			ip += 2;
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
		tmp16 += mem->read8(get_seg_adr(CS, ip));
		ip++;
	} else if (mod == 2) {
		tmp16 += mem->read16(get_seg_adr(CS, ip));
		ip += 2;
	}

	return tmp16;
}

u32 CPU::modrm32_ea(u8 modrm)
{
	// xxx 後で実装する
}

// modが11でないことはあらかじめチェックしておくこと
// Effective Addressを取得
// セグメント加算する
// ipはModR/Mの次をポイントしていなければならない
u32 CPU::modrm16_seg_ea(u8 modrm)
{
	return modrm16_ea(modrm)
		+ sdcr[modrm_add_seg[modrm >> 6][modrm & 7]].base;
}

u32 CPU::modrm_seg_ea(u8 modrm)
{
	if (addrsize == size16) {
		return modrm16_ea(modrm)
			+ sdcr[modrm_add_seg[modrm >> 6][modrm & 7]].base;
	} else {
		return modrm32_ea(modrm)
			+ sdcr[modrm_add_seg[modrm >> 6][modrm & 7]].base;
	}
}

// リアルモードでワード動作の場合
u16 CPU::modrm16w(u8 modrm)
{
	if (modrm >> 6 == 3) {
		return genregw(modrm & 7);
	}
	return mem->read16(modrm16_seg_ea(modrm));
}

u8 CPU::modrm16b(u8 modrm)
{
	if (modrm >> 6 == 3) {
		return *genregb[modrm & 7];
	}
	return mem->read8(modrm16_seg_ea(modrm));
}

void CPU::exec() {
	u8 op, subop;
//	u8 arg1, arg2, tmp1;
	u16 warg1, warg2;
	u8 modrm, ndisp, sreg, greg;
	u8 tmpb, tmpb2;
	u16 tmpw, tmpw2;
	u32 tmpd, tmpadr;
	u32 src, dst, res;

#ifdef CORE_DAS
	char str8x[8][4] = {"ADD", "OR", "ADC", "SBB", "AND", "SUB", "", "CMP"};
	char strdx[8][8] = {"ROL", "ROR", "RCL", "RCR", "SHL/SAL", "SHR", "", "SAR"};
	char strfx[8][5] = {"INC", "DEC", "CALL", "CALL", "JMP", "JMP", "PUSH", ""};
#endif
	if (seg_ovride == 0 && !opsize_ovride && !addrsize_ovride) {
		DAS_dump_reg();
	}
	op = mem->read8(get_seg_adr(CS, ip++));
	DAS_pr("%08x %02x", get_seg_adr(CS, ip - 1), op);

	switch (op) {
	case 0x00:
		break;

#define readb read8
#define readw read16
#define readd read32
#define writeb write8
#define writew write16
#define writed write32

/******************** ADD ********************/
/*
+--------+-----------+---------+---------+
|000000dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
OF/SF/ZF/AF/PF/CF:結果による
 */
	case 0x01: // ADD r/m16, r16 (ADD r/m32, r32)
		modrm = mem->read8(get_seg_adr(CS, ip));
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("ADD ");
		DAS_modrm16(modrm, true, false, true);
		ip++;
		// xxx アドレスサイズの差はマクロ化して吸収する
		if (opsize == size16) {
			src = genregw(modrm >> 3 & 7);
			if ((modrm & 0xc0) == 0xc0) {
				dst = genregw(modrm & 7);
				res = src + dst;
				genregw(modrm & 7) = res;
			} else {
				tmpadr = modrm_seg_ea(modrm);
				dst = mem->read16(tmpadr);
				res = src + dst;
				mem->write16(tmpadr, (u16)res);
			}
			ip++;
			flag8 = flag_calw[res];
			flag8 |= (src ^ dst ^ res) & AF;
			(res ^ src) & (res ^ dst) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
		} else {
			src = genregd(modrm >> 3 & 7);
			if ((modrm & 0xc0) == 0xc0) {
				dst = genregd(modrm & 7);
			        res = src + dst;
				genregd(modrm & 7) = res;
			} else {
				tmpadr = modrm_seg_ea(modrm);
				dst = mem->read32(tmpadr);
				res = src + dst;
				mem->write32(tmpadr, res);
			}
			ip++;
			// xxx ここは32bitは16bitとは違うフラグ計算
		}
		break;

	case 0x03: // ADD r16, r/m16 (ADD r32, r/m32)
		modrm = mem->read8(get_seg_adr(CS, ip));
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("ADD ");
		DAS_modrm16(modrm, true, true, true);
		tmpw = genregw(modrm >> 3 & 7);
		tmpw2 = modrm16w(modrm);
		tmpd = tmpw + tmpw2;
		genregw(modrm >> 3 & 7) = tmpd;
		ip++;
		flag8 = flag_calw[tmpd];
		flag8 |= (tmpw ^ tmpw2 ^ tmpd) & AF;
		(tmpd ^ tmpw) & (tmpd ^ tmpw2) & 0x8000?
			flagu8 |= OFSET8 : flagu8 &= OFCLR8;
		break;



/******************** OR ********************/
/*
+--------+-----------+---------+---------+
|000010dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
OF/CF:クリア, SF/ZF/PF:結果による, AF:不定
 */

#define bISWORD false
#define wISWORD true
// LOGical OPeration (OP r, r/m)
#define LOGOP_R_RM(op, str, bwd)			\
	modrm = mem->read8(get_seg_adr(CS, ip));	\
	greg = modrm >> 3 & 7;				\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr(#str" ");				\
	DAS_modrm16(modrm, true, true, bwd##ISWORD);	\
	tmp##bwd = genreg##bwd(greg);			\
	tmp##bwd op##= modrm16##bwd(modrm);		\
	genreg##bwd(greg) = tmp##bwd;			\
	flag8 = flag_cal##bwd[tmp##bwd];		\
	flagu8 &= OFCLR8;				\
	ip++;

	case 0x08: // OR r/m8, r8
		DAS_pr("xxxxx\n\n");
		break;
	case 0x09: // OR r/m16, r16
		DAS_pr("xxxxx\n\n");
		break;
	case 0x0a: // OR r8, r/m8
		LOGOP_R_RM(|, OR, b);
		break;
	case 0x0b: // OR r16, r/m16 (OR r32, r/m32)
		LOGOP_R_RM(|, OR, w);
		break;
	case 0x0c: // OR AL, imm8
		DAS_prt_post_op(1);
		DAS_pr("OR AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		al |= mem->read8(get_seg_adr(CS, ip));
		flag8 = flag_calb[al];
		flagu8 &= OFCLR8;
		ip++;
		break;

/******************** PUSH ********************/
// xxxセグメントオーバーライドされていても、call, pusha, enterではSSを使うらしい
#define PUSHW0(d)				\
	sp -= 2;				\
	mem->write16((segreg[SS] << 4) + sp, d)

#define PUSHW(d)				\
	sp -= 2;				\
	mem->write16(get_seg_adr(SS, sp), d)

#define PUSHW_GENREG(reg)		\
	DAS_prt_post_op(0);		\
	DAS_pr("PUSH "#reg"\n\n");	\
	PUSHW(reg)

#define PUSH_SEG(seg)			\
	DAS_prt_post_op(0);		\
	DAS_pr("PUSH "#seg"\n\n");	\
	PUSHW(segreg[seg])

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

	case 0x50: // PUSH AX
		PUSHW_GENREG(ax);
		break;
	case 0x51: // PUSH CX
		PUSHW_GENREG(cx);
		break;
	case 0x52: // PUSH DX
		PUSHW_GENREG(dx);
		break;
	case 0x53: // PUSH BX
		PUSHW_GENREG(bx);
		break;
	case 0x54: // PUSH SP
		PUSHW_GENREG(sp);
		break;
	case 0x55: // PUSH BP
		PUSHW_GENREG(bp);
		break;
	case 0x56: // PUSH SI
		PUSHW_GENREG(si);
		break;
	case 0x57: // PUSH DI
		PUSHW_GENREG(di);
		break;

	case 0x60: // PUSHA (PUSHAD)
		DAS_prt_post_op(0);
		DAS_pr("PUSHA\n\n");
		tmpw = sp;
		PUSHW0(ax);
		PUSHW0(cx);
		PUSHW0(dx);
		PUSHW0(bx);
		PUSHW0(tmpw);
		PUSHW0(bp);
		PUSHW0(si);
		PUSHW0(di);
		break;

	case 0x68: // PUSH imm16 (PUSH imm32)
		DAS_prt_post_op(2);
		tmpw = mem->read16(get_seg_adr(CS, ip));
		DAS_pr("PUSH 0x%04x\n\n", tmpw);
		PUSHW(tmpw);
		ip += 2;
		break;

	case 0x9c: // PUSHF (PUSHFD)
		DAS_prt_post_op(0);
		DAS_pr("PUSHF\n\n");
		PUSHW((u16)flagu8 << 8 | flag8);
		break;

/******************** POP ********************/
// xxxセグメントオーバーライドされていても、call, pusha, enterではSSを使うらしい
#define POPW0(d)				\
	d = mem->read16((segreg[SS] << 4) + sp);\
	sp += 2;

#define POPW(d)					\
	d = mem->read16(get_seg_adr(SS, sp));	\
	sp += 2;

#define POPW_GENREG(reg)		\
	DAS_prt_post_op(0);		\
	DAS_pr("POP "#reg"\n\n");	\
	POPW(reg)

#define POP_SEG(seg)			\
	DAS_prt_post_op(0);		\
	DAS_pr("POP "#seg"\n\n");	\
	POPW(tmpw);			\
	update_segreg(seg, tmpw)

	case 0x07: // POP ES
		POP_SEG(ES);
		break;
	case 0x17: // POP SS
		POP_SEG(SS);
		break;
	case 0x1f: // POP DS
		POP_SEG(DS);
		break;

	case 0x58: // POP AX
		POPW_GENREG(ax);
		break;
	case 0x59: // POP CX
		POPW_GENREG(cx);
		break;
	case 0x5a: // POP DX
		POPW_GENREG(dx);
		break;
	case 0x5b: // POP BX
		POPW_GENREG(bx);
		break;
	case 0x5c: // POP SP
		POPW_GENREG(sp);
		break;
	case 0x5d: // POP BP
		POPW_GENREG(bp);
		break;
	case 0x5e: // POP SI
		POPW_GENREG(si);
		break;
	case 0x5f: // POP DI
		POPW_GENREG(di);
		break;

	case 0x61: // POPA (POPAD)
		DAS_prt_post_op(0);
		DAS_pr("POPA\n\n");
		POPW0(di);
		POPW0(si);
		POPW0(bp);
		sp += 2;
		POPW0(bx);
		POPW0(dx);
		POPW0(cx);
		POPW0(ax);
		break;

	case 0x9d: // POPF (POPFD)
		DAS_prt_post_op(0);
		DAS_pr("POPF\n\n");
		POPW(tmpw);
		flagu8 = tmpw >> 8;
		flag8  = tmpw & 0xff;
		break;

/******************** AND ********************/
/*
+--------+-----------+---------+---------+
|001000dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
OF/CF:クリア, SF/ZF/PF:結果による, AF:不定
 */

	case 0x20: // AND r/m8, r8
	case 0x21: // AND r/m16, r16
	case 0x22: // AND r8, r/m8
		LOGOP_R_RM(&, AND, b);
		break;
	case 0x23: // AND r16, r/m16 (AND r32, r/m32)
		LOGOP_R_RM(&, AND, w);
		break;
/*
+--------+--------+-------------+
|0010010w|  data  |(data if w=1)|
+--------+--------+-------------+
OF/CF:クリア, SF/ZF/PF:結果による, AF:不定
 */
	case 0x24: // and al, imm8
		DAS_prt_post_op(1);
		DAS_pr("AND AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		al &= mem->read8(get_seg_adr(CS, ip++));
		flag8 = flag_calb[al];
		flagu8 &= OFCLR8;
		break;
	case 0x25: // AND AX, imm16 (AND EAX, imm32)
		DAS_prt_post_op(2);
		DAS_pr("AND AX, 0x%04x\n\n", mem->read16(get_seg_adr(CS, ip)));
		ax &= mem->read16(get_seg_adr(CS, ip));
		flag8 = flag_calw[ax];
		flagu8 &= OFCLR8;
		ip += 2;
		break;

/******************** SUB ********************/

	case 0x2a: // SUB r8, r/m8
		modrm = mem->read8(get_seg_adr(CS, ip));
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("SUB ");
		DAS_modrm16(modrm, true, true, false);
		ip++;
		tmpb = genregb(modrm >> 3 & 7);
		tmpb2 = modrm16b(modrm);
		tmpw = tmpb - tmpb2;
		genregb(modrm >> 3 & 7) = (u8)tmpw;
		flag8 = flag_calb[tmpw & 0x1ff];
		flag8 |= (tmpb ^ tmpb2 ^ tmpw) & AF;
		(tmpb ^ tmpw) & (tmpb ^ tmpb2) & 0x8000?
			flagu8 |= OFSET8 : flagu8 &= OFCLR8;
		break;

	case 0x2d: // SUB AX, imm16 (SUB EAX, imm32)
		DAS_prt_post_op(2);
		DAS_pr("SUB AX, 0x%04x\n\n", mem->read16(get_seg_adr(CS, ip)));
		tmpw = ax;
		tmpw2 = mem->read16(get_seg_adr(CS, ip));
		tmpd = tmpw - tmpw2;
		ax = (u16)tmpd;
		ip += 2;
		flag8 = flag_calw[tmpd & 0x1ffff];
		flag8 |= (tmpw ^ tmpw2 ^ tmpd) & AF;
		(tmpw ^ tmpd) & (tmpw ^ tmpw2) & 0x8000?
			flagu8 |= OFSET8 : flagu8 &= OFCLR8;
		break;

/******************** XOR ********************/
/*
          76  543 210
+--------+-----------+---------+---------+
|001100dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
OF/CF:クリア, SF/ZF/PF:結果による, AF:不定
 */

	case 0x32: // XOR r8, r/m8
		LOGOP_R_RM(^, XOR, b);
		break;

	case 0x33: // XOR r16, r/m16 (xxx XOR r32, r/m32)
		LOGOP_R_RM(^, XOR, w);
		break;

	case 0x34: // XOR AL, imm8
		DAS_prt_post_op(2);
		DAS_pr("XOR AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		al ^= mem->read8(get_seg_adr(CS, ip));
		ip++;
		flag8 = flag_calb[al];
		flagu8 &= OFCLR8;
		break;

/******************** CMP ********************/
/*
3C ib
CF/OF/SF/ZF/AF/PF:結果による
*/
	case 0x3c: // CMP AL, imm8
		DAS_prt_post_op(1);
		tmpb = mem->read8(get_seg_adr(CS, ip));
		DAS_pr("CMP AL, 0x%02x\n\n", tmpb);
		tmpw = al - tmpb; // CF用に17bit目が必要なので結果をwordに入れる
		ip++;
		flag8 = flag_calb[tmpw & 0x1ff];
		flag8 |= (al ^ tmpb ^ tmpw) & AF;
		(al ^ tmpw) & (al ^ tmpb) & 0x80?
			flagu8 |= OFSET8 : flagu8 &= OFCLR8;
		break;


/******************** INC ********************/
/*
CF:影響なし, OF/SF/ZF/AF/PF:結果による
 */
// xxx OFの計算がNP2と違う
#define INC_R16(reg)						\
	DAS_prt_post_op(0);					\
	DAS_pr("INC %s\n\n", #reg);				\
	tmpw = reg;						\
	reg++;							\
	flag8 &= CF; /* CF以外はリセット*/			\
	flag8 |= flag_calw[reg];				\
	flag8 |= (tmpw ^ reg) & AF;				\
	(tmpw ^ reg) & 0x8000?flagu8 |= OFSET8:flagu8 &= OFCLR8

	case 0x40: // INC AX (INC EAX)
		INC_R16(ax);
		break;
	case 0x41: // INC CX (INC ECX)
		INC_R16(cx);
		break;
	case 0x42: // INC DX (INC EDX)
		INC_R16(dx);
		break;
	case 0x43: // INC BX (INC EBX)
		INC_R16(bx);
		break;
	case 0x44: // INC SP (INC ESP)
		INC_R16(sp);
		break;
	case 0x45: // INC BP (INC EBP)
		INC_R16(bp);
		break;
	case 0x46: // INC SI (INC ESI)
		INC_R16(si);
		break;
	case 0x47: // INC DI (INC EDI)
		INC_R16(di);
		break;


/******************** DEC ********************/
/*
CF:影響なし, OF/SF/ZF/AF/PF:結果による
 */
// xxx OFの計算がNP2と違う
#define DEC_R16(reg)						\
	DAS_prt_post_op(0);					\
	DAS_pr("DEC %s\n\n", #reg);				\
	tmpw = reg;						\
	reg--;							\
	flag8 &= CF; /* CF以外はリセット*/			\
	flag8 |= flag_calw[reg];				\
	flag8 |= (tmpw ^ reg) & AF;				\
	(tmpw ^ reg) & 0x8000?flagu8 |= OFSET8:flagu8 &= OFCLR8

	case 0x48: // DEC AX (DEC EAX)
		DEC_R16(ax);
		break;
	case 0x49: // DEC CX (DEC ECX)
		DEC_R16(cx);
		break;
	case 0x4a: // DEC DX (DEC EDX)
		DEC_R16(dx);
		break;
	case 0x4b: // DEC BX (DEC EBX)
		DEC_R16(bx);
		break;
	case 0x4c: // DEC SP (DEC ESP)
		DEC_R16(sp);
		break;
	case 0x4d: // DEC BP (DEC EBP)
		DEC_R16(bp);
		break;
	case 0x4e: // DEC SI (DEC ESI)
		DEC_R16(si);
		break;
	case 0x4f: // DEC DI (DEC EDI)
		DEC_R16(di);
		break;

/******************** Jcc ********************/

	case 0x0f:
		subop = mem->read8(get_seg_adr(CS, ip));
		switch (subop) {
		case 0x84: // JE rel16 (JE rel32)
			DAS_prt_post_op(3);
			tmpw = mem->read16(get_seg_adr(CS, ++ip));
			DAS_pr("JE/JZ 0x%04x\n\n", tmpw);
			ip += 2;
			if (flag8 & ZF) {
				ip += (s16)tmpw;
			}
			break;
		}
		break;

	case 0x72: // JB rel8 or JC rel8 or JNAE rel8
		DAS_prt_post_op(1);
		DAS_pr("JB/JC/JNAE 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		if ((flag8 & CF)) {
			ip += (s8)mem->read8(get_seg_adr(CS, ip)) + 1;
		} else {
			ip++;
		}
		break;

	case 0x74: // JE rel8 or JZ rel8
		DAS_prt_post_op(1);
		DAS_pr("JE/JZ 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		if ((flag8 & ZF)) {
			ip += (s8)mem->read8(get_seg_adr(CS, ip)) + 1;
		} else {
			ip++;
		}
		break;

	case 0x75: // JNE rel8 or JNZ rel8
		DAS_prt_post_op(1);
		DAS_pr("JNE/JNZ 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		if (!(flag8 & ZF)) {
			ip += (s8)mem->read8(get_seg_adr(CS, ip)) + 1;
		} else {
			ip++;
		}
		break;

	case 0x78: // JS rel8
		DAS_prt_post_op(1);
		DAS_pr("JS 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		if (flag8 & SF) {
			ip += (s8)mem->read8(get_seg_adr(CS, ip)) + 1;
		} else {
			ip++;
		}
		break;

	case 0x79: // JNS rel8
		DAS_prt_post_op(1);
		DAS_pr("JNS 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		if (!(flag8 & SF)) {
			ip += (s8)mem->read8(get_seg_adr(CS, ip)) + 1;
		} else {
			ip++;
		}
		break;

	case 0x7d: // JGE rel8 or JNL rel8
		DAS_prt_post_op(1);
		tmpb = mem->read8(get_seg_adr(CS, ip));
		DAS_pr("JGE/JNL 0x%02x\n\n", tmpb);
		ip++;
		if (!((flag8 ^ flagu8 << 4) & 0x80)) { // SF == OF
			ip += (s8)tmpb;
		}
		break;

	case 0x7e: // JLE rel8 or JNG rel8
		DAS_prt_post_op(1);
		tmpb = mem->read8(get_seg_adr(CS, ip));
		DAS_pr("JLE/JNG 0x%02x\n\n", tmpb);
		ip++;
		 // ZF=1またはSF<>OF
		if (flag8 & ZF || (flag8 ^ flagu8 << 4) & 0x80) {
			ip += (s8)tmpb;
		}
		break;

/*
          76  543 210
+--------+-----------+---------+---------+--------+---------------+
|100000sw|mod ??? r/m|(DISP-LO)|(DISP-HI)|  data  |(data if sw=01)|
+--------+-----------+---------+---------+--------+---------------+
???(ここではregではなく、opの拡張。これにより以下の様に命令が変わる):
000:ADD, 001:OR, 010:ADC, 011:SBB, 100:AND, 101:SUB, 110:XOR, 111:CMP
 */
#define CAL_RM_IM(BWD, BWD2, BWD3, OP, CAST, CRY, IPINC, ANDN)		\
	ip++;								\
	if ((modrm & 0xc0) == 0xc0) {					\
		tmp##BWD = genreg##BWD(modrm & 7);			\
		tmp##BWD2##2 = mem->read##BWD2(get_seg_adr(CS, ip));	\
		tmp##BWD3 = tmp##BWD OP tmp##BWD2##2 + CRY;		\
		genreg##BWD(modrm & 7) = (CAST)tmp##BWD3;		\
	} else {							\
		tmpadr = modrm16_seg_ea(modrm);				\
		tmp##BWD = mem->read##BWD(tmpadr);			\
		tmp##BWD2##2 = mem->read##BWD2(get_seg_adr(CS, ip));	\
		tmp##BWD3 = tmp##BWD OP tmp##BWD2##2 + CRY;		\
		mem->write##BWD(tmpadr, (CAST)tmp##BWD3);		\
	}								\
	ip += IPINC;							\
	flag8 = flag_cal##BWD[tmp##BWD3 ANDN];				\
	flag8 |= (tmp##BWD ^ tmp##BWD2##2 ^ tmp##BWD3) & AF;

#define LOGOP_RM_IM(BWD, BWD2, OP, IPINC)				\
	ip++;								\
	if ((modrm & 0xc0) == 0xc0) {					\
		tmp##BWD = genreg##BWD(modrm & 7);			\
		tmp##BWD2##2 = mem->read##BWD2(get_seg_adr(CS, ip));	\
		tmp##BWD OP##= tmp##BWD2##2;				\
		genreg##BWD(modrm & 7) = tmp##BWD;			\
	} else {							\
		tmpadr = modrm16_seg_ea(modrm);				\
		tmp##BWD = mem->read##BWD(tmpadr);			\
		tmp##BWD2##2 = mem->read##BWD2(get_seg_adr(CS, ip));	\
		tmp##BWD OP##= tmp##BWD2##2;				\
		mem->write##BWD(tmpadr, tmp##BWD);			\
	}								\
	ip += IPINC;							\
	flag8 = flag_cal##BWD[tmp##BWD];				\
	flagu8 &= OFCLR8;

#define CMP_RM_IM(BWD, BWD2, BWD3, IPINC, ANDN, ANDN2)			\
	ip++;								\
	if ((modrm & 0xc0) == 0xc0) {					\
		tmp##BWD = genreg##BWD(modrm & 7);			\
		tmp##BWD2##2 = mem->read##BWD2(get_seg_adr(CS, ip));	\
		tmp##BWD3 = tmp##BWD - tmp##BWD2##2;			\
	} else {							\
		tmpadr = modrm16_seg_ea(modrm);				\
		tmp##BWD = mem->read##BWD(tmpadr);			\
		tmp##BWD2##2 = mem->read##BWD2(get_seg_adr(CS, ip));	\
		tmp##BWD3 = tmp##BWD - tmp##BWD2##2;			\
	}								\
	ip += IPINC;							\
	flag8 = flag_cal##BWD[tmp##BWD3 ANDN];				\
	flag8 |= (tmp##BWD ^ tmp##BWD2##2 ^ tmp##BWD3) & AF;		\
	(tmp##BWD ^ tmp##BWD3) & (tmp##BWD ^ tmp##BWD2##2) & ANDN2?	\
		flagu8 |= OFSET8 : flagu8 &= OFCLR8;

	case 0x80:
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
		DAS_pr("%s ", str8x[subop]);
		DAS_modrm16(modrm, false, false, false);
		DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, ip + nr_disp_modrm(modrm) + 1)));

		switch (subop) {
		case 0: // ADD r/m8, imm8
			CAL_RM_IM(b, b, w, +, u8, 0, 1, );
			(tmpb ^ tmpw) & (tmpb2 ^ tmpw) & 0x80?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			break;
		case 1: // OR r/m8, imm8
			LOGOP_RM_IM(b, b, |, 1);
			break;
		case 2: // ADC r/m8, imm8
			CAL_RM_IM(b, b, w, +, u8, (flag8 & CF), 1, );
			(tmpb ^ tmpw) & (tmpb2 ^ tmpw) & 0x80?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			break;
		case 3: // SBB r/m8, imm8
			CAL_RM_IM(b, b, w, -, u8, -(flag8 & CF), 1, & 0x1ff);
			(tmpb ^ tmpw) & (tmpb ^ tmpb2) & 0x80?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			break;
		case 4: // AND r/m8, imm8
			LOGOP_RM_IM(b, b, &, 1);
			printf("tmpadr=0x%x(0x%x)\n", tmpadr, tmpb);
			break;
		case 5: // SUB r/m8, imm8
			CAL_RM_IM(b, b, w, -, u8, 0, 1, & 0x1ff);
			(tmpb ^ tmpw) & (tmpb ^ tmpb2) & 0x80?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			break;
		case 6: // XOR r/m8, imm8
			LOGOP_RM_IM(b, b, ^, 1);
			break;
		case 7: // CMP r/m8, imm8
			CMP_RM_IM(b, b, w, 1, & 0x1ff, 0x80);
			break;
		}
		break;

	case 0x81:
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 3);
		DAS_pr("%s ", str8x[subop]);
		DAS_modrm16(modrm, false, false, true);
		DAS_pr("0x%04x\n\n", mem->read16(get_seg_adr(CS, ip + 1)));

		switch (subop) {

		case 0: // ADD r/m16, imm16
			CAL_RM_IM(w, w, d, +, u16, 0, 2, );
			(tmpw ^ tmpd) & (tmpw2 ^ tmpd) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			break;
		case 1: // OR r/m16, imm16
			LOGOP_RM_IM(w, w, |, 2);
			break;
		case 2: // ADC r/m16, imm16
			CAL_RM_IM(w, w, d, +, u16, (flag8 & CF), 2, );
			(tmpw ^ tmpd) & (tmpw2 ^ tmpd) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			break;
		case 3: // SBB r/m16, imm16
			CAL_RM_IM(w, w, d, -, u16, -(flag8 & CF), 2, & 0x1ffff);
			(tmpw ^ tmpd) & (tmpw ^ tmpw2) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			break;
		case 4: // AND r/m16, imm16
			LOGOP_RM_IM(w, w, &, 2);
			break;
		case 5: // SUB r/m16, imm16
			CAL_RM_IM(w, w, d, -, u16, 0, 2, & 0x1ffff);
			(tmpw ^ tmpd) & (tmpw ^ tmpw2) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			break;

		case 6: // XOR r/m16, imm16
			LOGOP_RM_IM(w, w, ^, 2);
			break;
		case 7: // CMP r/m16, imm16
			CMP_RM_IM(w, w, d, 2, & 0x1ffff, 0x8000);
			break;
		}
		break;

	case 0x83: // ADD/ADC/AND/SUB/SBB/CMP r/m16, imm8 (... r/m32, imm8)
		//w-bit 1なのでワード動作、s-bit 0なので即値は byte
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
		DAS_pr("%s ", str8x[subop]);
		DAS_modrm16(modrm, false, false, true);
		DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, ip + 1)));

		switch (subop) {
		case 0: // ADD r/m16, imm8
			CAL_RM_IM(w, b, d, +, u16, 0, 1, );
			(tmpw ^ tmpd) & (tmpb2 ^ tmpd) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			break;
		case 1: // OR r/m16, imm8
			LOGOP_RM_IM(w, b, |, 1);
			break;
		case 2: // ADC r/m16, imm8
			CAL_RM_IM(w, b, d, +, u16, (flag8 & CF), 1, );
			(tmpw ^ tmpd) & (tmpb2 ^ tmpd) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			break;
		case 3: // SBB r/m16, imm8
			CAL_RM_IM(w, b, d, -, u16, -(flag8 & CF), 1, & 0x1ffff);
			(tmpw ^ tmpd) & (tmpw ^ tmpb2) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			break;
		case 4: // AND r/m16, imm8
			LOGOP_RM_IM(w, b, &, 1);
			break;
		case 5: // SUB r/m16, imm8
			CAL_RM_IM(w, b, d, -, u16, 0, 1, & 0x1ffff);
			(tmpw ^ tmpd) & (tmpw ^ tmpb2) & 0x8000?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			break;
		case 6: // XOR r/m16, imm8
			LOGOP_RM_IM(w, b, ^, 1);
			break;
		case 7: // CMP r/m16, imm8
			CMP_RM_IM(w, b, d, 1, & 0x1ffff, 0x8000);
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
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("XCHG ");
		DAS_modrm16(modrm, true, true, false);
		ip++;
		if ((modrm & 0xc0) == 0xc0) {
			tmpb = genregb(modrm & 7);
			genregb(modrm & 7) = genregb(modrm >> 3 & 7);
			genregb(modrm >> 3 & 7) = tmpb;
		} else {
			tmpadr = modrm16_seg_ea(modrm);
			tmpb = mem->read8(tmpadr);
			mem->write8(tmpadr, genregb(modrm >> 3 & 7));
			genregb(modrm >> 3 & 7) = tmpb;
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
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm16(modrm, true, false, false);
		ip++;
		if ((modrm & 0xc0) == 0xc0) {
			genregb(modrm & 0x07) = *genregb[modrm >> 3 & 7];
		} else {
			mem->write8(modrm16_seg_ea(modrm), *genregb[modrm >> 3 & 7]);
		}
		break;

	case 0x89: // MOV r/m16, r16 (MOV r/m32, r32)
		DAS_pr("xxxxx\n\n");
		break;

	case 0x8a: // MOV r8, r/m8
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm16(modrm, true, true, false);
		ip++;
		*genregb[modrm >> 3 & 7] = modrm16b(modrm);
		break;

	case 0x8b: // MOV r16, r/m16 (MOV r32, r/m32)
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm16(modrm, true, true, true);
		ip++;
		genregw(modrm >> 3 & 7) = modrm16w(modrm);
		break;
/*
          76  543 210
+--------+-----------+---------+---------+
|10001100|mod 0SR r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
 */
	case 0x8c: // MOV r/m16, Sreg
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
		sreg = modrm >> 3 & 3;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm16(modrm, false, false, true);
		DAS_pr("%s\n\n", segreg_name[sreg]);
		ip++;
		if ((modrm & 0xc0) == 0xc0) {
			genregw(modrm & 0x07) = segreg[sreg];
		} else {
			mem->write16(modrm16_seg_ea(modrm), segreg[sreg]);
		}
		break;

/*
          76  543 210
+--------+-----------+---------+---------+
|10001101|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
 */
	case 0x8d: // LEA r16, m (LEA r32, m)
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
		tmpb = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("LEA ");
		DAS_modrm16(modrm, true, true, true);
		ip++;
		genregw(tmpb) = modrm16_ea(modrm);
		break;
/*
          76  543 210
+--------+-----------+---------+---------+
|10001110|mod 0SR r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
 */
	case 0x8e: // MOV Sreg, r/m16
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
		sreg = modrm >> 3 & 3;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV %s, ", segreg_name[sreg]);
		DAS_modrm16(modrm, false, true, true);
		ip++;
		update_segreg(sreg, modrm16w(modrm)); // セグメントはbaseも更新
		break;

/*
+---------+--------+--------+
|1010 000w|addr-lo |addr-hi |
+---------+--------+--------+
 */
	case 0xa0: // MOV AL, moffs8
		DAS_prt_post_op(2);
		DAS_pr("MOV AL, byte ptr [0x%04x]\n\n", mem->read16(get_seg_adr(CS, ip)));
		al = mem->read8(get_seg_adr(DS, mem->read16(get_seg_adr(CS, ip))));
		ip += 2;
		break;

	case 0xa1: // MOV AX, moffs16 (MOV EAX moffs32)
		DAS_prt_post_op(2);
		DAS_pr("MOV AX, word ptr [0x%04x]\n\n", mem->read16(get_seg_adr(CS, ip)));
		ax = mem->read16(get_seg_adr(DS, mem->read16(get_seg_adr(CS, ip))));
		ip += 2;
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
		DAS_pr("MOV %s, 0x%02x\n\n", genreg_name[0][op & 7], mem->read8(get_seg_adr(CS, ip)));
		*genregb[op & 7] = mem->read8(get_seg_adr(CS, ip++));
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
		DAS_prt_post_op(2);
		DAS_pr("MOV %s, 0x%04x\n\n", genreg_name[1][op & 7], mem->read16(get_seg_adr(CS, ip)));
		genregw(op & 7) = mem->read16(get_seg_adr(CS, ip));
		ip += 2;
		break;

/*
          76  543 210
+--------+-----------+---------+---------+--------+-------------+
|1100011w|mod 000 r/m|(DISP-LO)|(DISP-HI)|  data  |(data if w=1)|
+--------+-----------+---------+---------+--------+-------------+
 */
	case 0xc6: // MOV r/m8, imm8
		modrm = mem->read8(get_seg_adr(CS, ip));
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm16(modrm, false, false, false);
		DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, ip + nr_disp_modrm(modrm) + 1)));
		break;

/******************** TEST ********************/
// OF/CF:0, SF/ZF/PF:結果による, AF:未定義

	case 0x84: // TEST r/m8, r8
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
		tmpb = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("TEST ");
		DAS_modrm16(modrm, true, false, false);
		ip++;
		if ((modrm & 0xc0) == 0xc0) {
			tmpb = genregb(modrm & 0x07) & genregb(modrm >> 3 & 7);
		} else {
			tmpb = mem->read16(modrm16_seg_ea(modrm))
				& genregb(modrm >> 3 & 7);
		}
		flag8 = flag_calb[tmpb];
		flagu8 &= OFCLR8;
		break;

/*
+--------+--------+
|1010100w|  data  |
+--------+--------+
 */
	case 0xa8: // test al, imm8
		DAS_prt_post_op(1);
		DAS_pr("TEST AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		tmpb = al & mem->read8(get_seg_adr(CS, ip++));
		flag8 = flag_calb[tmpb];
		flagu8 &= OFCLR8;
		break;

/******************** CBW ********************/

	case 0x98:
		DAS_prt_post_op(0);
		DAS_pr("CBW\n\n");
		ah = (al & 0x80)? 0xff : 0x0 ;
		break;

/******************** LODS ********************/

	case 0xac:
		DAS_prt_post_op(0);
		DAS_pr("LODSB\n\n");
		al = mem->read8(sdcr[DS].base + si);
		si++;
		break;
	case 0xad:
		DAS_prt_post_op(0);
		DAS_pr("LODSW\n\n");
		ax = mem->read16(sdcr[DS].base + si);
		si +=2;
		break;

/******************** Rotate/Shift ********************/
/*
          76  543 210
+--------+-----------+---------+---------+--------+
|11000000|mod op2 r/m|(DISP-LO)|(DISP-HI)|  data  |
+--------+-----------+---------+---------+--------+
 */
	case 0xc0:
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		ndisp = nr_disp_modrm(modrm);
		DAS_prt_post_op(ndisp + 2);
		DAS_pr("%s ", strdx[subop]);
		DAS_modrm16(modrm, false, false, false);
		DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, ip + ndisp + 1)));
		switch (subop) {
		// C0 /5 ib
		case 0x5: // SHR r/m8, imm8
			ip++;
			tmpb = mem->read8(get_seg_adr(CS, ndisp + ip));
			if ((modrm & 0xc0) == 0xc0) {
				tmpb2 = genregb(modrm & 0x07);
				genregb(modrm & 0x07) = tmpb2 >> tmpb;
			} else {
				tmpadr = modrm16_seg_ea(modrm);
				tmpb2 = mem->read8(tmpadr);
				mem->write8(tmpadr, tmpb2 >> tmpb);
			}
			ip++;
			if (tmpb != 0) { // xxx フラグは要再確認
				flag8 = flag_calb[tmpb2 >> tmpb];
				flag8 |= AF; // NP2/NP21に合わせる
				// 元の値の7bitと6bitを比較する
				(tmpb2 ^ tmpb2 >> 1) & 0x40?
					flagu8 |= OFSET8 : flagu8 &= OFCLR8;
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
	case 0xc1:
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		ndisp = nr_disp_modrm(modrm);
		DAS_prt_post_op(ndisp + 2);
		DAS_pr("%s ", strdx[subop]);
		DAS_modrm16(modrm, false, false, true);
		DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, ip + ndisp + 1)));
		switch (subop) {
		// C1 /4 ib
		case 0x4: // SAL/SHL r/m16, imm8 (SAL/SHL r/m32, imm8)
			ip++;
			tmpb = mem->read8(get_seg_adr(CS, ndisp + ip));
			if ((modrm & 0xc0) == 0xc0) {
				tmpw = genregw(modrm & 0x07);
				genregw(modrm & 0x07) = tmpw << tmpb;
			} else {
				tmpadr = modrm16_seg_ea(modrm);
				tmpw = mem->read16(tmpadr);
				mem->write16(tmpadr, tmpw << tmpb);
			}
			ip++;
			flag8 = flag_calw[(u32)(tmpw << tmpb) & 0x1ffff];
			flag8 |= AF; // NP2/NP21に合わせる
			// 元の値の15bitと14bitを比較する
			(tmpw ^ tmpw >> 1) & 0x4000?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			break;
		// C1 /5 ib
		case 0x5: // SHR r/m16, imm8
			ip++;
			tmpb = mem->read8(get_seg_adr(CS, ndisp + ip));
			if ((modrm & 0xc0) == 0xc0) {
				tmpw2 = genregw(modrm & 0x07);
				genregw(modrm & 0x07) = tmpw2 >> tmpb;
			} else {
				tmpadr = modrm16_seg_ea(modrm);
				tmpw2 = mem->read16(tmpadr);
				mem->write16(tmpadr, tmpw2 >> tmpb);
			}
			ip++;
			if (tmpb != 0) { // xxx フラグは要再確認
				flag8 = flag_calw[tmpw2 >> tmpb];
				flag8 |= AF; // NP2/NP21に合わせる
				// 元の値の15bitと14bitを比較する
				(tmpw2 ^ tmpw2 >> 1) & 0x4000?
					flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			}
			break;
		default:
			DAS_pr("xxxxx\n\n");
		}
		break;

/*
          76  543 210
+--------+-----------+---------+---------+
|110100vw|mod op2 r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
 */
	case 0xd1:
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strdx[subop]);
		DAS_modrm16(modrm, false, true, true);
		switch (subop) {
		// D1 /4 r/m16
		case 0x4: // SAL/SHL r/m16 (SAL/SHL r/m32)
			ip++;
			if ((modrm & 0xc0) == 0xc0) {
				tmpd = genregw(modrm & 0x07) << 1;
				genregw(modrm & 0x07) = (u16)tmpd;
			} else {
				tmpadr = modrm16_seg_ea(modrm);
				tmpd = mem->read16(tmpadr) << 1;
				mem->write16(tmpadr, (u16)tmpd);
			}
			flag8 = flag_calw[tmpd];
			flag8 |= AF; // NP2/NP21に合わせる
			// 元の値の15bitと14bit比較だが、すでにtmpdは1左シフト
			// しているので、16bitと15bitを比較する
			(tmpd ^ tmpd << 1) & 0x10000?
				flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			break;
		default:
			DAS_pr("xxxxx\n\n");
		}
		break;

	case 0xd2:
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strdx[subop]);
		DAS_modrm16(modrm, false, false, false);
		DAS_pr("CL\n\n");
		switch (subop) {
		// D2 /4 r/m16
		case 0x4: // SAL/SHL r/m8, CL
			ip++;
			if ((modrm & 0xc0) == 0xc0) {
				tmpw = genregb(modrm & 0x07) << cl;
				genregb(modrm & 0x07) = (u8)tmpw;
			} else {
				tmpadr = modrm16_seg_ea(modrm);
				tmpw = mem->read8(tmpadr) << cl;
				mem->write8(tmpadr, (u8)tmpw);
			}
			if (cl != 0) {
				flag8 = flag_calb[tmpw & 0x1ff];
				flag8 |= AF; // NP2/NP21に合わせる
				// 元の値の7bitと6bit比較だが、すでにtmpwは
				// 1左シフトしているので、8bitと7bitを比較する
				(tmpw ^ tmpw << 1) & 0x100?
					flagu8 |= OFSET8 : flagu8 &= OFCLR8;
			}
			break;
		default:
			DAS_pr("xxxxx\n\n");
		}
		break;

/******************** RET ********************/

	case 0xc3: // RET  nearリターンする
		DAS_prt_post_op(0);
		DAS_pr("RET\n\n");
		POPW(ip);
		break;

	case 0xcb: // RET  farリターンする
		DAS_prt_post_op(0);
		DAS_pr("RET\n\n");
		POPW(ip);
		POPW(tmpw);
		update_segreg(CS, tmpw);
		break;

/******************** ESC ********************/

	case 0xdb: // ESC 3
		DAS_prt_post_op(1);
	        subop = mem->read8(get_seg_adr(CS, ip));
		switch (subop) {
		case 0xe3: // FNINIT
			printf("FNINIT\n\n");
			// nothing to do
			ip++;
			break;
		}
		break;

/******************** LOOP ********************/

	case 0xe2: // LOOP rel8
		DAS_prt_post_op(1);
		tmpb = mem->read8(get_seg_adr(CS, ip));
		DAS_pr("LOOP 0x%02x\n\n", tmpb);
		ip++;
		cx--;
		if (cx != 0) {
			ip += (s8)tmpb;
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
		DAS_pr("IN AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		al = io->read8(mem->read8(get_seg_adr(CS, ip++)));
		break;
	case 0xe5: // IN AX, imm8 (xxx IN EAX, imm8)
		DAS_prt_post_op(1);
		DAS_pr("IN AX, 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		ax = io->read16(mem->read8(get_seg_adr(CS, ip++)));
		break;

/*
+--------+--------+
|1110011w| data-8 |
+--------+--------+
 */
	case 0xe6: // OUT imm8, AL
		DAS_prt_post_op(1);
		DAS_pr("OUT 0x%02x, AL\n\n", mem->read8(get_seg_adr(CS, ip)));
		io->write8(mem->read8(get_seg_adr(CS, ip++)), al);
		break;
	case 0xe7: // OUT imm8, AX
		DAS_prt_post_op(1);
		DAS_pr("OUT 0x%02x, AX\n\n", mem->read8(get_seg_adr(CS, ip)));
		io->write16(mem->read8(get_seg_adr(CS, ip++)), ax);
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
		warg1 = mem->read16(get_seg_adr(CS, ip));
		ip += 2;
		DAS_pr("CALL 0x%04x\n\n", warg1);
		PUSHW0(ip);
		ip += (s16)warg1;
		break;
/*
+--------+--------+--------+--------+--------+
|10011010| IP-lo  | IP-hi  | CS-lo  | CS-hi  |
+--------+--------+--------+--------+--------+
 */
	case 0x9a: // CALL ptr16:16 セグメント外直接
		DAS_prt_post_op(4);
		warg1 = mem->read16(get_seg_adr(CS, ip));
		warg2 = mem->read16(get_seg_adr(CS, ip + 2));
		ip += 4;
		DAS_pr("CALL %04x:%04x\n\n", warg2, warg1);
		PUSHW0(segreg[CS]);
		PUSHW0(ip);
		update_segreg(CS, warg2);
		ip = warg1;
		break;

/******************** JMP ********************/
/*
+--------+---------+---------+
|11101001|IP-INC-LO|IP-INC-HI|
+--------+---------+---------+
 */
	case 0xe9: // JMP rel16 (JMP rel32) セグメント内直接ジャンプ
		DAS_prt_post_op(2);
		DAS_pr("JMP 0x%04x\n\n", mem->read16(get_seg_adr(CS, ip)));
		ip += (s16)mem->read16(get_seg_adr(CS, ip)) + 2;
		break;

/*
+--------+--------+--------+--------+--------+
|11101010| IP-lo  | IP-hi  | CS-lo  | CS-hi  |
+--------+--------+--------+--------+--------+
 */
	case 0xea: // セグメント外直接ジャンプ
		DAS_prt_post_op(4);
		warg1 = mem->read16(get_seg_adr(CS, ip));
		warg2 = mem->read16(get_seg_adr(CS, ip + 2));
		update_segreg(CS, warg2);
		ip = warg1;
		DAS_pr("JMP %04x:%04x\n\n", warg2, warg1);
		break;
/*
+--------+--------+
|11101011|IP-INC8 |
+--------+--------+
 */
	case 0xeb: //無条件ジャンプ/セグメントショート内直接
		DAS_prt_post_op(1);
		DAS_pr("JMP 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		ip += (s8)mem->read8(get_seg_adr(CS, ip)) + 1;
		break;

/******************** REP ********************/

	case 0xf3:
		DAS_prt_post_op(1);
		subop = mem->read8(get_seg_adr(CS, ip));
		switch (subop) {
		case 0xa4: // REP MOVS m8, m8
			DAS_pr("REP MOVSB\n\n");
			ip++;
			while (cx != 0) {
				mem->write8(get_seg_adr(DS, di), mem->read8(get_seg_adr(DS, si)));
				di++;
				si++;
				cx--;
			}
			break;
		default:
			DAS_pr("xxxxx\n\n");
		}
		break;


/******************** TEST/DEC/MUL/IMUL/DIV/IDIV/NOT ********************/

	case 0xf6:
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		switch (subop) {
/*
          76  543 210
+--------+-----------+---------+---------+--------+-----------+
|1111011w|mod 000 r/m|(DISP-LO)|(DISP-HI)|  data  |data if w=1|
+--------+-----------+---------+---------+--------+-----------+
OF/CF:0, SF/ZF/PF:結果による, AF:未定義
 */
		case 0x0: // TEST r/m8, imm8
			DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
			DAS_pr("TEST ");
			DAS_modrm16(modrm, false, false, false);
			DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, ip + nr_disp_modrm(modrm) + 1)));

			ip++;
			tmpb = modrm16b(modrm);
			tmpb &= mem->read8(get_seg_adr(CS, ip));
			ip++;
			flag8 = flag_calb[tmpb];
			flagu8 &= OFCLR8;
			break;
		}
		break;

	case 0xf7:
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		switch (subop) {
		case 0x6: // DIV r/m16
			DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
			DAS_pr("DIV ");
			DAS_modrm16(modrm, false, true, true);
			ip++;
			tmpd = (u32)(dx << 16) + ax;
			tmpw = modrm16w(modrm);
			ax = tmpd / tmpw;
			dx = tmpd % tmpw;
			break;
		}
		break;

/******************** セグメントオーバーライド ********************/

	case 0x26: // SEG=ES
		DAS_prt_post_op(0);
		DAS_pr("SEG=ES\n\n");
		seg_ovride++;
		sdcr[DS].base = sdcr[ES].base;
		sdcr[SS].base = sdcr[ES].base;
		if (seg_ovride >= 8) { // xxx ここら辺の情報不足
			// xxx ソフトウェア例外
		}
		return; // リターンする

	case 0x2e: // SEG=CS
		DAS_prt_post_op(0);
		DAS_pr("SEG=CS\n\n");
		seg_ovride++;
		sdcr[DS].base = sdcr[CS].base;
		sdcr[SS].base = sdcr[CS].base;
		if (seg_ovride >= 8) { // xxx ここら辺の情報不足
			// xxx ソフトウェア例外
		}
		return; // リターンする

/*************** オペランドサイズオーバーライドプリフィックス ***************/

	case 0x66:
		DAS_prt_post_op(0);
		DAS_pr("Ope Size Override\n\n");
		opsize_ovride = true;
		opsize = isRealMode? size32 : size16;
		return; // リターンする

/*************** アドレスサイズオーバーライドプリフィックス ***************/

	case 0x67:
		DAS_prt_post_op(0);
		DAS_pr("Addr Size Override\n\n");
		addrsize_ovride = true;
		addrsize = isRealMode? size32 : size16;
		return; // リターンする

/******************** プロセッサコントロール ********************/

	case 0xfb: // STI
		DAS_prt_post_op(0);
		DAS_pr("STI\n\n");
		flagu8 |= IFSET8;
		break;

	case 0xfe:
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strfx[subop]);
		DAS_modrm16(modrm, false, true, false);
		switch (subop) {
		case 0: // INC r/m8
			ip++;
			if ((modrm & 0xc0) == 0xc0) {
				// はみ出る可能性があるのでwordに格納
				tmpw = genregb(modrm & 7);
				tmpw2 = tmpw + 1;
				genregb(modrm & 7) = (u16)tmpw2;
			} else {
				tmpadr = modrm16_seg_ea(modrm);
				tmpw = mem->read16(tmpadr);
				tmpw2 = tmpw + 1;
				mem->write16(tmpadr, (u16)tmpw2);
			}
			flag8 &= CF; /* CF以外はリセット*/
			flag8 |= flag_calb[tmpw2];
			flag8 |= (tmpw ^ tmpw2) & AF;
			(tmpw ^ tmpw2) & 0x8000?
				flagu8 |= OFSET8:flagu8 &= OFCLR8;
			break;
		default:
			printf("xxxxx\n\n");
		}
		break;

	case 0xff: 
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strfx[subop]);
		DAS_modrm16(modrm, false, true, true);
		switch (subop) {
		case 2: // CALL r/m16 (CALL r/m32) 絶対関節nearコール
			ip++;
			if ((modrm & 0xc0) == 0xc0) {
				PUSHW0(ip);
				ip = genregw(modrm & 7);
			} else {
				tmpadr = modrm16_seg_ea(modrm);
				PUSHW0(ip);
				ip = mem->read16(tmpadr);
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
	}
	// オーバーライドしたセグメントを元に戻す
	if (seg_ovride == 0) {
		sdcr[DS].base = segreg[DS] << 4;
		sdcr[SS].base = segreg[SS] << 4;
	}

	// {オペランド|アドレス}サイズオーバーライドプリフィックスを元に戻す
	opsize_ovride = false;
	addrsize_ovride = false;
	opsize = isRealMode? size16 : size32;
	addrsize = isRealMode? size16 : size32;
}
