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
		pf = pf? 0 : PF;
		cf = (i & 0x100)? CF : 0;
		flag_calb[i] = sf | zf | pf | cf;
	}
	// ワード同士の演算によるフラグSF/ZF/PF/CFの状態をあらかじめ算出する
	// キャリーフラグ算出のため、配列長は17ビットである
	for (int i = 0; i < 0x20000; i++) {
		u8 sf, zf, cf;
		u16 pf;
		sf = (i & 0x8000)? SF : 0;
		zf = (i == 0)? ZF : 0;
		pf = i;
		pf ^= pf >> 8;
		pf ^= pf >> 4;
		pf ^= pf >> 2;
		pf ^= pf >> 1;
		pf = pf? 0 : PF;
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
	for (int i = 0; i < NR_SEGREG; i++) segreg[i] = 0x0000;
	segreg[CS] = 0xf000;
	ip = 0xfff0;
	edx = 0x12345678; // xxxなんか入れないとだめみたい
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

	for (i = 0; i < 4; i++) {
		printf("%s:%08x ", genreg_name[2][i], genreg32(i));
	}
	printf("  eflags:%08x", (u16)flagu8 << 8 | flag8);
	printf("\n");
	for (i = 4; i < NR_GENREG; i++) {
		printf("%s:%08x ", genreg_name[2][i], genreg32(i));
	}
	printf("     eip:%08x", ip);
	printf("\n");
	for (i = 0; i < NR_SEGREG; i++) {
		printf("%s:%04x ", segreg_name[i], segreg[i]);
	}
	printf("\n\n");
}

void CPU::DAS_prt_post_op(u8 n) {
	int i;
	for (i = 0; i < n; i++)
		printf(" %02x", mem->read8(get_seg_adr(CS, ip + i)));
	for (i = 0; i < 5 - n; i++)
		printf("%3c", ' ');
}

// modR/Mに続くディスプレースメントのバイト数を返す
u8 CPU::DAS_nr_disp_modrm(u8 modrm) {
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
		printf(", %s", genreg_name[isWord][reg]);
	}
}

#define DAS_pr(...) printf(__VA_ARGS__)
#else
#define DAS_dump_reg()
#define DAS_prt_post_op(n)
#define DAS_nr_disp_modrm(m)
#define DAS_modrm16(m, isR, isD, isW)
#define DAS_pr(...)
#endif // CORE_DAS

// modが11でないことはあらかじめチェックしておくこと
// Effective Addressを取得
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

// リアルモードでワード動作の場合
u16 CPU::modrm16w(u8 modrm)
{
	if (modrm >> 6 == 3) {
		return genregw(modrm & 7);
	}
	return mem->read16(get_seg_adr(DS, modrm16_ea(modrm)));
}

u8 CPU::modrm16b(u8 modrm)
{
	if (modrm >> 6 == 3) {
		return *genregb[modrm & 7];
	}
	return mem->read8(get_seg_adr(DS, modrm16_ea(modrm)));
}

#define bISWORD false
#define wISWORD true
// LOGical OPeration (OP r, r/m)
#define LOGOP_R_RM(op, str, bwd)			\
	modrm = mem->read8(get_seg_adr(CS, ip));	\
	rm = modrm & 7;					\
	DAS_prt_post_op(DAS_nr_disp_modrm(modrm) + 1);	\
	DAS_pr(#str" ");				\
	DAS_modrm16(modrm, true, true, bwd##ISWORD);	\
	tmp##bwd = genreg##bwd(rm);			\
	tmp##bwd op##= modrm16##bwd(modrm);		\
	genreg##bwd(rm) = tmp##bwd;			\
	flag8 = flag_cal##bwd[tmp##bwd];		\
	flagu8 &= OFCLR8;				\
	ip++;

void CPU::exec() {
	u8 op, subop;
//	u8 arg1, arg2, tmp1;
	u16 tmp16, warg1, warg2;
	u8 modrm, rm, sreg;
	u8 tmpb;
	u16 tmpw;

#ifdef CORE_DAS
	char str8x[8][4] = {"ADD", "", "ADC", "SBB", "", "SUB", "", "CMP"};
#endif
	DAS_dump_reg();
	op = mem->read8(get_seg_adr(CS, ip++));
	DAS_pr("%08x %02x", get_seg_adr(CS, ip - 1), op);

	switch (op) {
	case 0x00:
		break;

/******************** OR ********************/
/*
+--------+-----------+---------+---------+
|000010dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
OF/CF:クリア, SF/ZF/PF:結果による, AF:不定
 */

	case 0x08: // OR r/m8, r8
		break;
	case 0x09: // OR r/m16, r16
		break;
	case 0x0a: // OR r8, r/m8
		LOGOP_R_RM(|, OR, b);
		break;
	case 0x0b: // OR r16, r/m16 (OR r32, r/m32)
		LOGOP_R_RM(|, OR, w);
		break;

/******************** PUSH ********************/
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

	case 0x0e: // PUSH CS
		PUSH_SEG(CS);
		break;
	case 0x16: // PUSH SS
		PUSH_SEG(SS);
		break;
	case 0x1e: // PUSH DS
		PUSH_SEG(DS);
		break;
	case 0x06: // PUSH ES
		PUSH_SEG(ES);
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
		tmp16 = sp;
		PUSHW(ax);
		PUSHW(cx);
		PUSHW(dx);
		PUSHW(bx);
		PUSHW(tmp16);
		PUSHW(bp);
		PUSHW(si);
		PUSHW(di);
		break;

	case 0x68: // PUSH imm16 (PUSH imm32)
		DAS_prt_post_op(2);
		tmp16 = mem->read16(get_seg_adr(CS, ip));
		DAS_pr("PUSH 0x%04x\n\n", tmp16);
		PUSHW(tmp16);
		ip += 2;
		break;

/******************** POP ********************/
#define POPW(d)					\
	d = mem->read16(get_seg_adr(SS, sp));	\
	sp += 2;

	case 0x07: // POP ES
		DAS_prt_post_op(0);
		DAS_pr("POP ES\n\n");
		POPW(tmp16);
		update_segreg(ES, tmp16);
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
		DAS_pr("AND AX, 0x%02x\n\n", mem->read16(get_seg_adr(CS, ip)));
		ax &= mem->read16(get_seg_adr(CS, ip));
		flag8 = flag_calw[ax];
		flagu8 &= OFCLR8;
		ip += 2;
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

	case 0x75: // JNE, imm8
		DAS_prt_post_op(1);
		DAS_pr("JNE 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		if (!(flag8 & ZF)) { // xxx マイナスの時の考慮必要
			ip += mem->read8(get_seg_adr(CS, ip)) + 1;
		} else {
			ip++;
		}
		break;

/******************** INC ********************/
/*
CF:影響なし, OF/SF/ZF/PF:結果による, AF:不定
 */
#define INC_R16(reg) \
	DAS_prt_post_op(0);			\
	DAS_pr("INC %s\n\n", #reg);		\
	tmp16 = reg;				\
	reg++;					\
	flag8 &= CF; /* CF以外はリセット*/	\
	flag8 |= flag_calw[reg];		\
	flag8 |= (tmp16 ^ reg) & AF;

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

/*
          76  543 210
+--------+-----------+---------+---------+--------+---------------+
|100000sw|mod ??? r/m|(DISP-LO)|(DISP-HI)|  data  |(data if sw=01)|
+--------+-----------+---------+---------+--------+---------------+
???(ここではregではなく、opの拡張。これにより以下の様に命令が変わる):
000:ADD, 010:ADC, 100:AND, 101:SUB, 011:SBB, 111:CMP
 */
	case 0x83: // ADD/ADC/AND/SUB/SBB/CMP r/m16, imm8 (... r/m32, imm8)
		//w-bit 1なのでワード動作、s-bit 0なので即値は byte
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(DAS_nr_disp_modrm(modrm) + 2);
		DAS_pr("%s ", str8x[subop]);
		DAS_modrm16(modrm, false, false, true);
		DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, ip + 1)));
		// xxx SUB DX, imm8 のみ実装
		switch (subop) {
		case 5: // SUB
			if (modrm >> 6 == 3) {
				genregw(modrm & 7) -= mem->read8(get_seg_adr(CS, ++ip));
				ip++;
			} else {
				// xxx
			}
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
		DAS_prt_post_op(DAS_nr_disp_modrm(modrm) + 1);
		DAS_pr("XCHG ");
		DAS_modrm16(modrm, true, true, false);
		ip++;
		if ((modrm & 0xc0) == 0xc0) {
			tmpb = genregb(modrm & 7);
			genregb(modrm & 7) = genregb(modrm >> 3 & 7);
			genregb(modrm >> 3 & 7) = tmpb;
		} else {
			tmpw = modrm16_ea(modrm);
			tmpb = mem->read8(get_seg_adr(DS, tmpw));
			mem->write8(get_seg_adr(DS, tmpw), genregb(modrm >> 3 & 7));
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
	case 0x8a: // MOV r8, r/m8
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
		DAS_prt_post_op(DAS_nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm16(modrm, true, true, false);
		ip++;
		*genregb[modrm >> 3 & 3] = modrm16b(modrm);
		break;
	case 0x8b: // MOV r16, r/m16 (MOV r32, r/m32)
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
		DAS_prt_post_op(DAS_nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm16(modrm, true, true, true);
		ip++;
		genregw(modrm >> 3 & 3) = modrm16w(modrm);
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
		DAS_prt_post_op(DAS_nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm16(modrm, false, false, true);
		DAS_pr("%s\n\n", segreg_name[sreg]);
		ip++;
		if ((modrm & 0xc0) == 0xc0) {
			genregw(modrm & 0x07) = segreg[sreg];
		} else {
			mem->write16(get_seg_adr(DS, modrm16_ea(modrm)), segreg[sreg]);
		}
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
		DAS_prt_post_op(DAS_nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV %s, ", segreg_name[sreg]);
		DAS_modrm16(modrm, false, true, true);
		ip++;
		update_segreg(sreg, modrm16w(modrm)); // セグメントはbaseも更新
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

/******************** TEST ********************/
/*
+--------+--------+
|1010100w|  data  |
+--------+--------+
OF/CF:0, SF/ZF/PF:結果による, AF:未定義
 */
	case 0xa8: // test al, imm8
		DAS_prt_post_op(1);
		DAS_pr("TEST AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		tmpb = al & mem->read8(get_seg_adr(CS, ip++));
		flag8 = flag_calb[tmpb];
		flagu8 &= OFCLR8;
		break;

/******************** Rotate/Shift ********************/

	case 0xc0:
		DAS_prt_post_op(4);
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		switch (subop) {
		// C0 /5 ib
		case 0x5: // SHR r/m8, imm8
			printf("hogehoge");
			break;
		}
		break;

/******************** RET ********************/

	case 0xc3: // RET
		DAS_prt_post_op(0);
		DAS_pr("RET\n\n");
		POPW(ip);
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
		PUSHW(ip);
		ip += warg1; // xxx マイナスを要考慮
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
		PUSHW(ip);
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
		// xxx マイナスの時の考慮必要
		ip += mem->read16(get_seg_adr(CS, ip)) + 2;
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
		// xxx マイナスの時の考慮必要
		ip += mem->read8(get_seg_adr(CS, ip)) + 1;
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
			DAS_prt_post_op(DAS_nr_disp_modrm(modrm) + 2);
			DAS_pr("TEST ");
			DAS_modrm16(modrm, false, false, false);
			DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, ip + DAS_nr_disp_modrm(modrm) + 1)));

			ip++;
			tmpb = modrm16b(modrm);
			tmpb &= mem->read8(get_seg_adr(CS, ip));
			ip++;
			flag8 = flag_calb[tmpb];
			flagu8 &= OFCLR8;
			break;
		}
		break;

/******************** プロセッサコントロール ********************/

	case 0xfb: // STI
		DAS_prt_post_op(0);
		DAS_pr("STI\n\n");
		flagu8 |= IFSET8;
		break;

	case 0xff: 
		DAS_prt_post_op(1);
		DAS_pr("xxx\n\n");
		break;

	default:
		DAS_prt_post_op(0);
		printf("xxxxxxxxxx\n\n");
	}
}
