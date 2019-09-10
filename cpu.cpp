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
// isWord: ワード(ダブルワード)転送かバイト転送か
// POP m16でregのないModR/Mでコンマ不要の場合はisReg=false, isDest=trueにする
void CPU::DAS_modrm16(u8 modrm, bool isReg, bool isDest, bool isWord) {
	u8 mod, reg, rm;
#define NR_RM 8
	char str[NR_RM][9] = {"[BX + SI", "[BX + DI", "[BP + SI", "[BP + DI", "[SI", "[DI", "[BP", "[BX", };
	char s[] = " + 0x????";

	REGSIZE regsize;
	regsize = isWord? (opsize == size16? word : dword) : byte;
	char sizestr[3][6] = {"byte", "word", "dword"};

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
		printf("%s\n\n", genreg_name[regsize][reg]);
	}
}

#define DAS_pr(...) printf(__VA_ARGS__)

void CPU::DAS_prt_rmr_rrm(const char *s, bool isReg, bool isDest, bool isWord)
{
	u8 modrm;
	modrm = mem->read8(get_seg_adr(CS, ip));
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
	DAS_pr("%s ", s);
	DAS_modrm16(modrm, isReg, isDest, isWord);
}
#else
#define DAS_dump_reg()
#define DAS_prt_post_op(n)
#define DAS_modrm16(m, isR, isD, isW)
#define DAS_pr(...)
#define DAS_prt_rmr_rrm()
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
// xxx 32bit命令を実装後はmodrm_seg_ea()を使用し、この関数は不要となる
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

// リアルモードでダブルワード動作の場合
// (オペランドサイズオーバーライドプリフィックスを使った場合)
u32 CPU::modrm16d(u8 modrm)
{
	if (modrm >> 6 == 3) {
		return genregd(modrm & 7);
	}
	return mem->read32(modrm16_seg_ea(modrm));
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
	u16 warg1, warg2;
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
	op = mem->read8(get_seg_adr(CS, ip++));
	DAS_pr("%08x %02x", get_seg_adr(CS, ip - 1), op);

	switch (op) {

#define readb read8
#define readw read16
#define readd read32
#define writeb write8
#define writew write16
#define writed write32

#define bISWORD false
#define wISWORD true
#define dISWORD true

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
	modrm = mem->read8(get_seg_adr(CS, ip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr(#STR" ");				\
	DAS_modrm16(modrm, true, false, BWD##ISWORD);	\
	ip++;						\
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
	modrm = mem->read8(get_seg_adr(CS, ip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr(#STR" ");				\
	DAS_modrm16(modrm, true, false, BWD##ISWORD);	\
	ip++;						\
	dst = genreg##BWD(modrm >> 3 & 7);		\
	src = modrm16##BWD(modrm);			\
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
		CAL_R_RM(+, ADD, w, 0, );
		break;
	case 0x04: // ADD AL, imm8
		DAS_prt_post_op(1);
		DAS_pr("ADD AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		src = mem->read8(get_seg_adr(CS, ip));
		ip++;
		res = al + src;
		FLAG8b(res, src, al, );
		OF_ADDb(res, src, al);
		al = res;
		break;
	case 0x05: // ADD AX, imm16 (ADD EAX, imm32)
		DAS_prt_post_op(2);
		DAS_pr("ADD AX, 0x%04x\n\n", mem->read16(get_seg_adr(CS, ip)));
		src = mem->read16(get_seg_adr(CS, ip));
		ip += 2;
		res = ax + src;
		FLAG8w(res, src, ax, );
		OF_ADDw(res, src, ax);
		ax = res;
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
		DAS_pr("ADC AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		src = mem->read8(get_seg_adr(CS, ip));
		ip++;
		res = al + src + (flag8 & CF);
		FLAG8b(res, src, al, );
		OF_ADCb(res, src, al);
		al = res;
		break;
	case 0x15: // ADC AX, imm16 (ADC EAX, imm32)
		DAS_prt_post_op(2);
		DAS_pr("ADC AX, 0x%04x\n\n", mem->read16(get_seg_adr(CS, ip)));
		src = mem->read16(get_seg_adr(CS, ip));
		ip += 2;
		res = ax + src + (flag8 & CF);
		FLAG8w(res, src, ax, );
		OF_ADCw(res, src, ax);
		ax = res;
		break;

/******************** OR ********************/
/*
+--------+-----------+---------+---------+
|000010dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
OF/CF:クリア, SF/ZF/PF:結果による, AF:不定
 */

// LOGical OPeration (OP r, r/m)
#define LOGOP_R_RM(OP, STR, BWD)			\
	modrm = mem->read8(get_seg_adr(CS, ip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr(#STR" ");				\
	DAS_modrm16(modrm, true, true, BWD##ISWORD);	\
	ip++;						\
	greg = modrm >> 3 & 7;				\
	dst = genreg##BWD(greg);			\
	dst OP##= modrm16##BWD(modrm);			\
	genreg##BWD(greg) = dst;			\
	flag8 = flag_cal##BWD[dst];			\
	flagu8 &= ~OFSET8;

// LOGical OPeration (OP r/m, r)
#define LOGOP_RM_R(OP, STR, BWD)			\
	modrm = mem->read8(get_seg_adr(CS, ip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr(#STR" ");				\
	DAS_modrm16(modrm, true, false, BWD##ISWORD);	\
	rm = modrm & 7;					\
	ip++;						\
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
	flag8 = flag_cal##BWD[dst];			\
	flagu8 &= ~OFSET8;

	case 0x08: // OR r/m8, r8
		LOGOP_RM_R(|, OR, b);
		break;
	case 0x09: // OR r/m16, r16
		LOGOP_RM_R(|, OR, w);
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
		flagu8 &= ~OFSET8;
		ip++;
		break;
	case 0x0d: // OR AX, imm16 (OR EAX, imm32)
		DAS_prt_post_op(2);
		DAS_pr("OR AX, 0x%04x\n\n", mem->read16(get_seg_adr(CS, ip)));
		ax |= mem->read16(get_seg_adr(CS, ip));
		flag8 = flag_calw[ax];
		flagu8 &= ~OFSET8;
		ip += 2;
		break;

/******************** PUSH ********************/
// xxxセグメントオーバーライドされていても、call, pusha, enterではSSを使うらしい
#define PUSHW0(d)				\
	sp -= 2;				\
	mem->write16((segreg[SS] << 4) + sp, (u16)d)

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
		dst = sp;
		PUSHW0(ax);
		PUSHW0(cx);
		PUSHW0(dx);
		PUSHW0(bx);
		PUSHW0(dst);
		PUSHW0(bp);
		PUSHW0(si);
		PUSHW0(di);
		break;

	case 0x68: // PUSH imm16 (PUSH imm32)
		DAS_prt_post_op(2);
		dst = mem->read16(get_seg_adr(CS, ip));
		DAS_pr("PUSH 0x%04x\n\n", dst);
		PUSHW(dst);
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
	POPW(dst);			\
	update_segreg(seg, (u16)dst)

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

/*
+--------+-----------+---------+---------+
|10001111|mod 000 r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
 */
	case 0x8f: // POP m16 (POP m32)
		modrm = mem->read8(get_seg_adr(CS, ip));
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("POP ");
		DAS_modrm16(modrm, false, true, false);
		ip++;
		if ((modrm & 0xc0) == 0xc0) {
			genregw(modrm & 7) = mem->read16(get_seg_adr(SS, sp));
		} else {
			mem->write16(modrm16_seg_ea(modrm),
				     mem->read16(get_seg_adr(SS, sp)));
		}
		sp += 2;
		break;

	case 0x9d: // POPF (POPFD)
		DAS_prt_post_op(0);
		DAS_pr("POPF\n\n");
		POPW(dst);
		flagu8 = (u8)(dst >> 8);
		flag8  = dst & 0xff;
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
	case 0x21: // AND r/m16, r16
		LOGOP_RM_R(&, AND, b);
		break;
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
		flagu8 &= ~OFSET8;
		break;
	case 0x25: // AND AX, imm16 (AND EAX, imm32)
		DAS_prt_post_op(2);
		DAS_pr("AND AX, 0x%04x\n\n", mem->read16(get_seg_adr(CS, ip)));
		ax &= mem->read16(get_seg_adr(CS, ip));
		flag8 = flag_calw[ax];
		flagu8 &= ~OFSET8;
		ip += 2;
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
		tmpb = mem->read8(get_seg_adr(CS, ip));
		if (tmpb != 0x0a) {
			// imm8が0x0aでなければ本当はミーモニックなし
			DAS_pr("AAM 0x%02x\n\n", tmpb);
		} else {
			DAS_pr("AAM\n\n");
		}
		ip++;
		ah = al / tmpb;
		al = al % tmpb;
		flag8 = flag_calb[al]; // CFは未定義(OF, AFも未定義)
		break;
	case 0xd5:
		DAS_prt_post_op(1);
		tmpb = mem->read8(get_seg_adr(CS, ip));
		if (tmpb != 0x0a) {
			DAS_pr("AAD 0x%02x\n\n", tmpb);
		} else {
			DAS_pr("AAD\n\n");
		}
		ip++;
		al += (ah + tmpb) & 0xff;
		ah = 0;
		flag8 = flag_calb[al];
		break;

/******************** SBB ********************/

	case 0x18: // SBB r/m8, r8
		CAL_RM_R(-, SBB, b, -(flag8 & CF), & 0x1ff);
		break;
	case 0x19: // SBB r/m16, r16 (SBB r/m32, r32)
		CAL_RM_R(-, SBB, w, -(flag8 & CF), & 0x1ffff);
		break;
	case 0x1a: // SBB r8, r/m8
		CAL_R_RM(-, SBB, b, -(flag8 & CF), & 0x1ff);
		break;
	case 0x1b: // SBB r16, r/m16 (SBB r32, r/m32)
		CAL_R_RM(-, SBB, w, -(flag8 & CF), & 0x1ffff);
		break;
	case 0x1c: // SBB AL, imm8
		DAS_prt_post_op(1);
		DAS_pr("SBB AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		dst = al;
		src = mem->read8(get_seg_adr(CS, ip));
		res = dst - src - (flag8 & CF);
		al = (u8)res;
		ip ++;
		FLAG8b(res, src, dst, & 0x1ff);
		OF_SBBb(res, src, dst);
		break;
	case 0x1d: // SBB AX, imm16 (SBB EAX, imm32)
		DAS_prt_post_op(2);
		DAS_pr("SBB AX, 0x%04x\n\n", mem->read16(get_seg_adr(CS, ip)));
		dst = ax;
		src = mem->read16(get_seg_adr(CS, ip));
		res = dst - src - (flag8 & CF);
		ax = (u16)res;
		ip += 2;
		FLAG8w(res, src, dst, & 0x1ffff);
		OF_SBBw(res, src, dst);
		break;

/******************** SUB ********************/

	case 0x28: // SUB r/m8, r8
		CAL_RM_R(-, SUB, b, 0, & 0x1ff);
		break;
	case 0x29: // SUB r/m16, r16 (SUB r/m32, r32)
		CAL_RM_R(-, SUB, w, 0, & 0x1ffff);
		break;
	case 0x2a: // SUB r8, r/m8
		CAL_R_RM(-, SUB, b, 0, & 0x1ff);
		break;
	case 0x2b: // SUB r16, r/m16 (SUB r32, r/m32)
		CAL_R_RM(-, SUB, w, 0, & 0x1ffff);
		break;
	case 0x2c: // SUB AL, imm8
		DAS_prt_post_op(1);
		DAS_pr("SUB AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		dst = al;
		src = mem->read8(get_seg_adr(CS, ip));
		res = dst - src;
		al = (u8)res;
		ip ++;
		FLAG8b(res, src, dst, & 0x1ff);
		OF_SUBb(res, src, dst);
		break;
	case 0x2d: // SUB AX, imm16 (SUB EAX, imm32)
		DAS_prt_post_op(2);
		DAS_pr("SUB AX, 0x%04x\n\n", mem->read16(get_seg_adr(CS, ip)));
		dst = ax;
		src = mem->read16(get_seg_adr(CS, ip));
		res = dst - src;
		ax = (u16)res;
		ip += 2;
		FLAG8w(res, src, dst, & 0x1ffff);
		OF_SUBw(res, src, dst);
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
		LOGOP_RM_R(^, XOR, w);
		break;
	case 0x32: // XOR r8, r/m8
		LOGOP_R_RM(^, XOR, b);
		break;
	case 0x33: // XOR r16, r/m16 (xxx XOR r32, r/m32)
		LOGOP_R_RM(^, XOR, w);
		break;
	case 0x34: // XOR AL, imm8
		DAS_prt_post_op(1);
		DAS_pr("XOR AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		al ^= mem->read8(get_seg_adr(CS, ip));
		ip++;
		flag8 = flag_calb[al];
		flagu8 &= ~OFSET8;
		break;
	case 0x35: // XOR AX, imm16 (XOR EAX, imm32)
		DAS_prt_post_op(2);
		DAS_pr("XOR AX, 0x%04x\n\n", mem->read16(get_seg_adr(CS, ip)));
		ax ^= mem->read16(get_seg_adr(CS, ip));
		ip += 2;
		flag8 = flag_calw[ax];
		flagu8 &= ~OFSET8;
		break;

/******************** CMP ********************/

#define CMP_R_RM(BWD)					\
	modrm = mem->read8(get_seg_adr(CS, ip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr("CMP ");					\
	DAS_modrm16(modrm, true, true, BWD##ISWORD);	\
	ip++;						\
	dst = genreg##BWD(modrm >> 3 & 7);		\
	src = modrm16##BWD(modrm);			\
	res = dst - src;				\
	flag8 = flag_cal##BWD[dst];			\
	/* xxx AFの計算が必要 */			\
	flagu8 &= ~OFSET8;


#define CMP_RM_R(BWD)					\
	modrm = mem->read8(get_seg_adr(CS, ip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr("CMP ");					\
	DAS_modrm16(modrm, true, false, BWD##ISWORD);	\
	rm = modrm & 7;					\
	ip++;						\
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
		src = mem->read8(get_seg_adr(CS, ip));
		DAS_pr("CMP AL, 0x%02x\n\n", src);
		res = al - src;
		ip++;
		flag8 = flag_calb[res & 0x1ff];
		flag8 |= (al ^ src ^ res) & AF;
		(al ^ res) & (al ^ src) & 0x80?
			flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
		break;
	case 0x3d: // CMP AX, imm16 (CMP EAX, imm32)
		DAS_prt_post_op(2);
		src = mem->read16(get_seg_adr(CS, ip));
		DAS_pr("CMP AX, 0x%04x\n\n", src);
		res = ax - src;
		ip += 2;
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
	dst = reg;						\
	reg--;							\
	flag8 &= CF; /* CF以外はリセット*/			\
	flag8 |= flag_calw[reg];				\
	flag8 |= (dst ^ reg) & AF;				\
	(dst ^ reg) & 0x8000?flagu8 |= OFSET8:flagu8 &= ~OFSET8

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
			dst = mem->read16(get_seg_adr(CS, ++ip));
			DAS_pr("JE/JZ 0x%04x\n\n", dst);
			ip += 2;
			if (flag8 & ZF) {
				ip += (s16)dst;
			}
			break;
		default:
			DAS_pr("xxxxx\n\n");
			// LFS/LGS/LSS... (80386)
		}
		break;

#define JCC(STR, COND)							\
	DAS_prt_post_op(1);						\
	DAS_pr(#STR"0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));	\
	if (COND) {						   	\
		ip += (s8)mem->read8(get_seg_adr(CS, ip)) + 1;		\
	} else {							\
		ip++;							\
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
		JCC(JNLE/JG, !(flag8 & ZF) && (flag8 ^ flagu8 << 4) & 0x80);
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
	ip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 7);			\
		src = mem->read##BWD2(get_seg_adr(CS, ip));	\
		res = dst OP src + CRY;				\
		genreg##BWD(modrm & 7) = (CAST)res;		\
	} else {						\
		tmpadr = modrm16_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		src = mem->read##BWD2(get_seg_adr(CS, ip));	\
		res = dst OP src + CRY;				\
		mem->write##BWD(tmpadr, (CAST)res);		\
	}						       	\
	ip += IPINC;						\
	flag8 = flag_cal##BWD[res ANDN];			\
	flag8 |= (dst ^ src ^ res) & AF;

#define LOGOP_RM_IM(BWD, BWD2, OP, IPINC)			\
	ip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 7);			\
		src = mem->read##BWD2(get_seg_adr(CS, ip));	\
		dst OP##= src;					\
		genreg##BWD(modrm & 7) = dst;			\
	} else {						\
		tmpadr = modrm16_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		src = mem->read##BWD2(get_seg_adr(CS, ip));	\
		dst OP##= src;					\
		mem->write##BWD(tmpadr, dst);			\
	}							\
	ip += IPINC;						\
	flag8 = flag_cal##BWD[dst];				\
	flagu8 &= ~OFSET8;

#define CMP_RM_IM(BWD, BWD2, IPINC, ANDN, ANDN2)		\
	ip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 7);			\
		src = mem->read##BWD2(get_seg_adr(CS, ip));	\
		res = dst - src;				\
	} else {						\
		tmpadr = modrm16_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		src = mem->read##BWD2(get_seg_adr(CS, ip));	\
		res = dst - src;				\
	}							\
	ip += IPINC;						\
	flag8 = flag_cal##BWD[res ANDN];			\
	flag8 |= (dst ^ src ^ res) & AF;			\
	(dst ^ res) & (dst ^ src) & ANDN2?			\
		flagu8 |= OFSET8 : flagu8 &= ~OFSET8;

	case 0x80:
		// go through
	case 0x82:
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
		DAS_pr("%s ", str8x[subop]);
		DAS_modrm16(modrm, false, false, false);
		DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, ip + nr_disp_modrm(modrm) + 1)));

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
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 3);
		DAS_pr("%s ", str8x[subop]);
		DAS_modrm16(modrm, false, false, true);
		DAS_pr("0x%04x\n\n", mem->read16(get_seg_adr(CS, ip + 1)));

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
		case 4: // AND r/m16, imm16
			LOGOP_RM_IM(w, w, &, 2);
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
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
		DAS_pr("%s ", str8x[subop]);
		DAS_modrm16(modrm, false, false, true);
		DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, ip + 1)));

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
	modrm = mem->read8(get_seg_adr(CS, ip));		\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);		\
	DAS_pr("XCHG ");					\
	DAS_modrm16(modrm, true, true, BWD##ISWORD);		\
	greg = modrm >> 3 & 7;					\
	ip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		rm = modrm & 7;					\
		dst = genreg##BWD(rm);				\
		genreg##BWD(rm) = genreg##BWD(greg);		\
		genreg##BWD(greg) = dst;			\
	} else {						\
		tmpadr = modrm16_seg_ea(modrm);			\
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
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm16(modrm, true, false, false);
		ip++;
		if ((modrm & 0xc0) == 0xc0) {
			genregb(modrm & 0x07) = genregb(modrm >> 3 & 7);
		} else {
			mem->write8(modrm16_seg_ea(modrm), genregb(modrm >> 3 & 7));
		}
		break;

	case 0x89: // MOV r/m16, r16 (MOV r/m32, r32)
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm16(modrm, true, false, true);
		ip++;
		if ((modrm & 0xc0) == 0xc0) {
			genregw(modrm & 0x07) = genregw(modrm >> 3 & 7);
		} else {
			mem->write16(modrm16_seg_ea(modrm), genregw(modrm >> 3 & 7));
		}
		break;

	case 0x8a: // MOV r8, r/m8
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm16(modrm, true, true, false);
		ip++;
		genregb(modrm >> 3 & 7) = modrm16b(modrm);
		break;

	case 0x8b: // MOV r16, r/m16 (MOV r32, r/m32)
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("MOV ");
		DAS_modrm16(modrm, true, true, true);
		ip++;
		if (opsize == size16) {
			genregw(modrm >> 3 & 7) = modrm16w(modrm);
		} else {
			genregd(modrm >> 3 & 7) = modrm16d(modrm);
		}
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
		greg = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("LEA ");
		DAS_modrm16(modrm, true, true, true);
		ip++;
		genregw(greg) = modrm16_ea(modrm);
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
	case 0xa2: // MOV moffs8, AL
		DAS_prt_post_op(2);
		DAS_pr("MOV AL, byte ptr [0x%04x]\n\n", mem->read16(get_seg_adr(CS, ip)));
		mem->write8(get_seg_adr(DS, mem->read16(get_seg_adr(CS, ip))), al);
		ip += 2;
		break;
	case 0xa3: // MOV moffs16, AX (MOV moffs32, EAX)
		DAS_prt_post_op(2);
		DAS_pr("MOV AX, word ptr [0x%04x]\n\n", mem->read16(get_seg_adr(CS, ip)));
		mem->write16(get_seg_adr(DS, mem->read16(get_seg_adr(CS, ip))), ax);
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
		if (opsize == size16) {
			DAS_prt_post_op(2);
			DAS_pr("MOV %s, 0x%04x\n\n", genreg_name[1][op & 7], mem->read16(get_seg_adr(CS, ip)));
			genregw(op & 7) = mem->read16(get_seg_adr(CS, ip));
			ip += 2;
		} else {
			DAS_prt_post_op(4);
			DAS_pr("MOV %s, 0x%08x\n\n", genreg_name[2][op & 7], mem->read32(get_seg_adr(CS, ip)));
			genregd(op & 7) = mem->read32(get_seg_adr(CS, ip));
			ip += 4;
		}
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
		ip++;
		if ((modrm & 0xc0) == 0xc0) {
			genregb(modrm & 7) = mem->read8(get_seg_adr(CS, ip));
		} else {
			mem->write8(modrm16_seg_ea(modrm), mem->read8(get_seg_adr(CS, ip)));
			ip++;
		}
		break;
	case 0xc7: // MOV r/m16, imm16
		modrm = mem->read8(get_seg_adr(CS, ip));
		DAS_prt_post_op(nr_disp_modrm(modrm) + 2);
		DAS_pr("MOV ");
		DAS_modrm16(modrm, false, false, true);
		DAS_pr("0x%04x\n\n", mem->read16(get_seg_adr(CS, ip + nr_disp_modrm(modrm) + 1)));
		ip++;
		if ((modrm & 0xc0) == 0xc0) {
			genregw(modrm & 7) = mem->read16(get_seg_adr(CS, ip));
		} else {
			mem->write16(modrm16_seg_ea(modrm), mem->read16(get_seg_adr(CS, ip)));
			ip += 2;
		}
		break;

/******************** TEST ********************/
// OF/CF:0, SF/ZF/PF:結果による, AF:未定義

#define TEST_RM_R(BWD)						\
	modrm = mem->read8(get_seg_adr(CS, ip));		\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);		\
	DAS_pr("TEST ");					\
	DAS_modrm16(modrm, true, false, BWD##ISWORD);		\
	ip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 0x07)			\
			& genreg##BWD(modrm >> 3 & 7);		\
	} else {						\
		dst = mem->read##BWD(modrm16_seg_ea(modrm))	\
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
		DAS_pr("TEST AL, 0x%02x\n\n", mem->read8(get_seg_adr(CS, ip)));
		dst = al & mem->read8(get_seg_adr(CS, ip++));
		flag8 = flag_calb[dst];
		flagu8 &= ~OFSET8;
		break;
	case 0xa9: // test ax, imm16 (test eax, imm32)
		DAS_prt_post_op(2);
		DAS_pr("TEST AX, 0x%04x\n\n", mem->read16(get_seg_adr(CS, ip)));
		dst = ax & mem->read16(get_seg_adr(CS, ip));
		ip += 2;
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
		DAS_pr("STOSW\n\n");
	        (repe_prefix)? cnt = cx, cx = 0 : cnt = 1;
		incdec = (flagu8 & DF8)? -2 : +2;
		while (cnt != 0) {
			mem->write16(get_seg_adr(ES, di), ax);
			di += incdec;
			cnt--;
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
			ax = mem->read16(get_seg_adr(DS, si));
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
	case 0xaf:
		DAS_prt_post_op(0);
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
		break;

/******************** Rotate/Shift ********************/
/*
          76  543 210
+--------+-----------+---------+---------+--------+
|11000000|mod op2 r/m|(DISP-LO)|(DISP-HI)|  data  |
+--------+-----------+---------+---------+--------+
 */
	case 0xc0: // 80386
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
			src = mem->read8(get_seg_adr(CS, ndisp + ip));
			if ((modrm & 0xc0) == 0xc0) {
				dst = genregb(modrm & 0x07);
				genregb(modrm & 0x07) = dst >> src;
			} else {
				tmpadr = modrm16_seg_ea(modrm);
				dst = mem->read8(tmpadr);
				mem->write8(tmpadr, dst >> src);
			}
			ip++;
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
			src = mem->read8(get_seg_adr(CS, ndisp + ip));
			if ((modrm & 0xc0) == 0xc0) {
				dst = genregw(modrm & 0x07);
				genregw(modrm & 0x07) = dst << src;
			} else {
				tmpadr = modrm16_seg_ea(modrm);
				dst = mem->read16(tmpadr);
				mem->write16(tmpadr, dst << src);
			}
			ip++;
			flag8 = flag_calw[dst << src & 0x1ffff];
			flag8 |= AF; // NP2/NP21に合わせる
			// 元の値の15bitと14bitを比較する
			(dst ^ dst >> 1) & 0x4000?
				flagu8 |= OFSET8 : flagu8 &= ~OFSET8;
			break;
		// C1 /5 ib
		case 0x5: // SHR r/m16, imm8
			ip++;
			src = mem->read8(get_seg_adr(CS, ndisp + ip));
			if ((modrm & 0xc0) == 0xc0) {
				dst = genregw(modrm & 0x07);
				genregw(modrm & 0x07) = dst >> src;
			} else {
				tmpadr = modrm16_seg_ea(modrm);
				dst = mem->read16(tmpadr);
				mem->write16(tmpadr, dst >> src);
			}
			ip++;
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
	ip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 0x07);		\
		FUNC;						\
		genreg##BWD(modrm & 0x07) = (BWD##CAST)res;	\
	} else {						\
		tmpadr = modrm16_seg_ea(modrm);			\
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
	ip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		res = genreg##BWD(modrm & 0x07) << CNT;		\
		genreg##BWD(modrm & 0x07) = (BWD##CAST)res;	\
	} else {						\
		tmpadr = modrm16_seg_ea(modrm);			\
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
	ip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 0x07);		\
		res = dst >> CNT;				\
		genregb(modrm & 0x07) = (BWD##CAST)res;		\
	} else {						\
		tmpadr = modrm16_seg_ea(modrm);			\
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
	ip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 7);			\
		res = dst >> CNT;				\
		if (dst & SFT_AND2##BWD) {			\
			res |= sar_bit##BWD[CNT];		\
		}						\
		genreg##BWD(modrm & 7) = (BWD##CAST)res;	\
	} else {						\
		tmpadr = modrm16_seg_ea(modrm);			\
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
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strdx[subop]);
		DAS_modrm16(modrm, false, true, true);
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
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strdx[subop]);
		DAS_modrm16(modrm, false, true, true);
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
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strdx[subop]);
		DAS_modrm16(modrm, false, false, false);
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
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strdx[subop]);
		DAS_modrm16(modrm, false, false, true);
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
		// ipは後で書き換わるのであらかじめ取得しておく
		src = mem->read16(get_seg_adr(CS, ip));
		DAS_pr("RET 0x%04x\n\n", src);
		POPW(ip);
		sp += src;
		break;
	case 0xca: // RET  farリターンする
		DAS_prt_post_op(1);
		src = mem->read16(get_seg_adr(CS, ip));
		DAS_pr("RET 0x%04x\n\n", src);
		POPW(ip);
		POPW(dst);
		update_segreg(CS, (u16)dst);
		sp += src;
		break;

/******************** LES/LDS ********************/

#define LxS(STR, seg)							\
	modrm = mem->read8(get_seg_adr(CS, ip));			\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);			\
	DAS_pr(#STR" ");						\
	ip++;								\
	DAS_modrm16(modrm, true, true, true);				\
	if ((modrm & 0xc0) == 0xc0) {					\
		/* xxx ソフトウェア例外らしい */			\
	} else {/* xxx NP2に合わせたが、genregとESの順番が逆かも */	\
		tmpadr = modrm16_seg_ea(modrm);				\
		genregw(modrm >> 3 & 7) = mem->read16(tmpadr);		\
		update_segreg(seg, mem->read16(tmpadr + 2));		\
	}

	case 0xc4: // LES r16, m16:16 (LES r32, m16:32)
		LxS(LES, ES);
		break;
	case 0xc5: // LDS r16, m16:16 (LDS r32, m16:32)
		LxS(LDS, DS);
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
	        subop = mem->read8(get_seg_adr(CS, ip));
		switch (subop) {
		case 0xe3: // FNINIT
			printf("FNINIT\n\n");
			// nothing to do
			ip++;
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

/******************** TEST/NOT/NEG/MUL/IMUL/DIV/IDIV ********************/

	case 0xf6:
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + (subop < 2)?2:1);
		DAS_pr("%s ", strf6[subop]);
		DAS_modrm16(modrm, false, (subop < 2)?false:true, false);
		ip++;
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
			DAS_pr("0x%02x\n\n", mem->read8(get_seg_adr(CS, ip + nr_disp_modrm(modrm))));
			flag8 = flag_calb[modrm16b(modrm)
					  & mem->read8(get_seg_adr(CS, ip))];
			flagu8 &= ~OFSET8;
			ip++;
			break;
		case 0x2: // NOT r/m8
			if ((modrm & 0xc0) == 0xc0) {
				rm = modrm & 7;
				genregb(rm) = ~genregb(rm);
			} else {
				tmpadr = modrm16_seg_ea(modrm);
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
				tmpadr = modrm16_seg_ea(modrm);
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
				ax = mem->read8(modrm16_seg_ea(modrm)) * al;
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
				ax = (s8)mem->read8(modrm16_seg_ea(modrm)) * (s8)al;
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
			src = modrm16b(modrm);
			al = dst / src;
			ah = dst % src;
			break;
		case 0x7: // IDIV r/m8
			dst = ax;
			src = modrm16b(modrm);
			al = (s16)dst / (s8)src;
			ah = (s16)dst % (s8)src;
			break;
		}
		break;

	case 0xf7:
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + (subop < 2)?3:1);
		DAS_pr("%s ", strf6[subop]);
		DAS_modrm16(modrm, false, (subop < 2)?false:true, true);
		ip++;
		switch (subop) {
		case 0x0: // TEST r/m16, imm16 (TEST r/m32, imm32)
			// go through
		case 0x1:
			DAS_pr("0x%04x\n\n", mem->read16(get_seg_adr(CS, ip + nr_disp_modrm(modrm))));
			flag8 = flag_calw[modrm16w(modrm)
					  & mem->read16(get_seg_adr(CS, ip))];
			flagu8 &= ~OFSET8;
			ip += 2;
			break;
		case 0x2: // NOT r/m16
			if ((modrm & 0xc0) == 0xc0) {
				rm = modrm & 7;
				genregw(rm) = ~genregw(rm);
			} else {
				tmpadr = modrm16_seg_ea(modrm);
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
				tmpadr = modrm16_seg_ea(modrm);
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
				res = mem->read8(modrm16_seg_ea(modrm)) * ax;
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
				res = (s16)mem->read8(modrm16_seg_ea(modrm)) * (s16)ax;
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
			src = modrm16w(modrm);
			ax = dst / src;
			dx = dst % src;
			break;
		case 0x7: // IDIV r/m16
			dst = (u32)(dx << 16) + ax;
			src = modrm16w(modrm);
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
		opsize = isRealMode? size32 : size16;
		return; // リターンする

/*************** アドレスサイズオーバーライドプリフィックス ***************/

	case 0x67:
		DAS_prt_post_op(0);
		DAS_pr("Addr Size Override\n\n");
		addrsize_ovride = true;
		addrsize = isRealMode? size32 : size16;
		return; // リターンする

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

/******************** プロセッサコントロール ********************/

	case 0xfb: // STI
		DAS_prt_post_op(0);
		DAS_pr("STI\n\n");
		flagu8 |= IFSET8;
		break;

/******************** INC/DEC/CALL/JMP/PUSH ********************/

#define INC_RM(BWD, OP)						\
	if ((modrm & 0xc0) == 0xc0) {				\
		dst = genreg##BWD(modrm & 7);			\
		res = dst OP 1;					\
		genreg##BWD(modrm & 7) = (BWD##CAST)res;	\
	} else {						\
		tmpadr = modrm16_seg_ea(modrm);			\
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
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strfe[subop]);
		DAS_modrm16(modrm, false, true, false);
		ip++;
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
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
		DAS_prt_post_op(nr_disp_modrm(modrm) + 1);
		DAS_pr("%s ", strff[subop]);
		DAS_modrm16(modrm, false, true, true);
		ip++;
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
				ip = genregw(modrm & 7);
			} else {
				tmpadr = modrm16_seg_ea(modrm);
				PUSHW0(ip);
				ip = mem->read16(tmpadr);
			}
			break;
		case 3: // CALL m16:16 (CALL m16:32) 絶対間接farコール
			if ((modrm & 0xc0) == 0xc0) {
				// xxx ソフトウェア例外らしい
			} else {
				tmpadr = modrm16_seg_ea(modrm);
				warg1 = mem->read16(tmpadr);
				warg2 = mem->read16(tmpadr + 2);
				PUSHW0(segreg[CS]);
				PUSHW0(ip);
				update_segreg(CS, warg2);
				ip = warg1;
			}
			break;
		case 4: // JMPL r/m16 (JMP r/m32) 絶対間接nearジャンプ
			if ((modrm & 0xc0) == 0xc0) {
				ip = genregw(modrm & 7);
			} else {
				ip = mem->read16(modrm16_seg_ea(modrm));
			}
			break;
		case 5: // JMPL r/m16 (JMP r/m32) 絶対間接faジャンプ
			if ((modrm & 0xc0) == 0xc0) {
				// xxx ソフトウェア例外らしい
			} else {
				tmpadr = modrm16_seg_ea(modrm);
				update_segreg(CS, mem->read16(tmpadr + 2));
				ip = mem->read16(tmpadr);
			}
			break;
		case 6: // PUSH r/m16 (PUSH r/m32)
			if ((modrm & 0xc0) == 0xc0) {
				PUSHW_GENREG(genregw(modrm & 7));
			} else {
				PUSHW(mem->read16(modrm16_seg_ea(modrm)));
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
			sdcr[DS].base = segreg[DS] << 4;
			sdcr[SS].base = segreg[SS] << 4;
		}
	}

	// {オペランド|アドレス}サイズオーバーライドプリフィックスを元に戻す
	opsize_ovride = false;
	addrsize_ovride = false;
	repne_prefix = false;
	repe_prefix = false;
	opsize = isRealMode? size16 : size32;
	addrsize = isRealMode? size16 : size32;
}
