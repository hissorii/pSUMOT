#include <cstdio> // for printf()
#include "cpu.h"

using namespace std;

CPU::CPU(BUS* bus) {
	mem = bus->get_bus("mem");
	io = bus->get_bus("io");
}

void CPU::dump_reg() {
	int i;

	for (i = 0; i < 4; i++) {
		printf("%s:%08x ", genreg_name32[i], genreg32(i));
	}
	printf("\n");
	for (i = 4; i < NR_GENREG; i++) {
		printf("%s:%08x ", genreg_name32[i], genreg32(i));
	}
	printf("\n");
	for (i = 0; i < NR_SEGREG; i++) {
		printf("%s:%04x ", segreg_name[i], segreg[i]);
	}
	printf("\n\n");
}

// - 引数aにセグメントを加算したアドレスを返却する
// - 386では、セグメント加算は、プロテクトモードでもリアルモードでも、
//   セグメントディスクリプターキャッシュ内のbase addressに対して行う
u32 CPU::get_seg_adr(const SEGREG seg, const u16 a) {
	return sdcr[seg].base + a;
}

void CPU::update_segreg(const SEGREG seg, const u16 n) {
	// リアルモードでは、SDCRのbaseを更新するだけ
	segreg[seg] = n;
	sdcr[seg].base = segreg[seg] << 4;
}

void CPU::reset() {
	segreg[CS] = 0xf000;
	for (int i = DS; i <= GS; i++) segreg[i] = 0x0000;
	ip = 0xfff0;
	edx = 0x00000000; // xxxなんか入れないとだめみたい
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
}

void CPU::prt_post_op(u8 n) {
	for (int i = 0; i < n; i++)
		printf(" %02x", mem->read8(get_seg_adr(CS, ip + i)));
}

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

	rm = modrm & 3;
	if (mod == 0 && rm == 6) return 2;

	return 0;
}

// リアルモードでワード動作の場合
u16 CPU::modrm16w(u8 modrm) {
	u8 mod, rm;
	u16 tmp16;

	mod = modrm >> 6;
	rm = modrm & 3;

	if (mod == 3) {
		return genreg16(rm);
	}

	switch (rm) {
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

	return mem->read16(get_seg_adr(DS, tmp16));
}

u8 CPU::modrm16b(u8 modrm) {
	u8 mod, rm;
	mod = modrm >> 6;
	rm = modrm & 3;
	if (mod == 3) {
		return *genreg8[rm];
	}
}

// リアルモードでワード動作の場合
void CPU::disas_modrm16w(u8 modrm) {
	u8 mod, reg, rm;
#define NR_RM 8
	char str[NR_RM][9] = {"[BX + SI", "[BX + DI", "[BP + SI", "[BP + DI", "[SI", "[DI", "[BP", "[BX", };
	char s[] = " + 0x????";

	reg = modrm >> 3 & 3;
	printf("%s, ", genreg_name16[reg]);
	mod = modrm >> 6;
	rm = modrm & 3;

	if (mod == 3) {
		printf("%s\n\n", genreg_name16[rm]);
		return;
	}

	printf("word ptr ");

	if (rm == 6 && mod == 0) {
		printf("[0x%04x]\n\n", mem->read16(get_seg_adr(CS, ip + 1)));
		return;
	}

	if (mod == 1) {
		sprintf(s, " + 0x%02x", mem->read8(get_seg_adr(CS, ip + 1)));
	} else if (mod == 2) {
		sprintf(s, " + 0x%04x", mem->read16(get_seg_adr(CS, ip + 1)));
	} else {
		s[0] = '\0';
	}
	printf("%s%s]\n\n", str[rm], s);
}

void CPU::exec() {
	u8 op;
	u8 arg1, arg2, tmp1;
	u16 warg1, warg2;

	dump_reg();
	op = mem->read8(get_seg_adr(CS, ip++));

	printf("%08x %02x", get_seg_adr(CS, ip - 1), op);

	switch (op) {
	case 0x00:
		break;
	case 0x8b: // mov.w reg, X
		u8 modrm;
		modrm = mem->read8(get_seg_adr(CS, ip));
		prt_post_op(nr_disp_modrm(modrm) + 1);
		printf("\tMOV ");
		disas_modrm16w(modrm);

		arg1 = mem->read8(get_seg_adr(CS, ip++)); // modR/Mを読み込む
		genreg16(arg1 >> 3 & 3) = modrm16w(arg1);
		break;
	case 0xba: // mov dx, Imm16
		prt_post_op(2);
		printf("\t MOV dx, 0x%04x\n\n", mem->read16(get_seg_adr(CS, ip)));

		dx = mem->read16(get_seg_adr(CS, ip));
		ip += 2;
		break;
	case 0xea: // セグメント外直接ジャンプ
		prt_post_op(4);

		warg1 = mem->read16(get_seg_adr(CS, ip));
		warg2 = mem->read16(get_seg_adr(CS, ip + 2));
		update_segreg(CS, warg2);
		ip = warg1;

		printf("\tJMP %04x:%04x\n\n", warg2, warg1);
		break;
	}
}
