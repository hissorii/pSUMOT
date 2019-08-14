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
		printf("%s:%08x ", genreg_name[2][i], genreg32(i));
	}
	printf("\n");
	for (i = 4; i < NR_GENREG; i++) {
		printf("%s:%08x ", genreg_name[2][i], genreg32(i));
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
}

void CPU::prt_post_op(u8 n) {
	int i;
	for (i = 0; i < n; i++)
		printf(" %02x", mem->read8(get_seg_adr(CS, ip + i)));
	for (i = 0; i < 5 - n; i++)
		printf("%3c", ' ');
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

	rm = modrm & 7;
	if (mod == 0 && rm == 6) return 2;

	return 0;
}

u16 CPU::modrm16_sub(u8 modrm)
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
		return genreg16(modrm & 7);
	}
	return mem->read16(get_seg_adr(DS, modrm16_sub(modrm)));
}

u8 CPU::modrm16b(u8 modrm)
{
	if (modrm >> 6 == 3) {
		return *genreg8[modrm & 7];
	}
	return mem->read8(get_seg_adr(DS, modrm16_sub(modrm)));
}

// リアルモード動作の場合
// isReg: mod reg R/M の reg が存在するか
// isDest: mod reg R/M の reg がDestinationになるか
// isWord: ワード転送かバイト転送か
void CPU::disas_modrm16(u8 modrm, bool isReg, bool isDest, bool isWord) {
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

void CPU::exec() {
	u8 op, subop;
//	u8 arg1, arg2, tmp1;
	u16 warg1, warg2;
	u8 modrm;
#ifdef CORE_DBG
	char str8x[8][4] = {"ADD", "", "ADC", "SBB", "", "SUB", "", "CMP"};
#endif
	dump_reg();
	op = mem->read8(get_seg_adr(CS, ip++));
#ifdef CORE_DBG
	printf("%08x %02x", get_seg_adr(CS, ip - 1), op);
#endif
	switch (op) {
	case 0x00:
		break;

/*
          76  543 210
+--------+-----------+---------+---------+
|001100dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
 */
	case 0x32: // XOR.b reg, modR/M
		modrm = mem->read8(get_seg_adr(CS, ip));
#ifdef CORE_DBG
		prt_post_op(nr_disp_modrm(modrm) + 1);
		printf("XOR ");
		disas_modrm16(modrm, true, true, false);
#endif
		*genreg8[modrm & 7] ^= modrm16b(modrm);
		ip++;
		break;

	case 0x33:
		modrm = mem->read8(get_seg_adr(CS, ip));
#ifdef CORE_DBG
		prt_post_op(nr_disp_modrm(modrm) + 1);
		printf("XOR ");
		disas_modrm16(modrm, true, true, true);
#endif
		genreg16(modrm & 7) ^= modrm16w(modrm);
		ip++;
		break;

/*
          76  543 210
+--------+-----------+---------+---------+--------+---------------+
|100000sw|mod ??? r/m|(DISP-LO)|(DISP-HI)|  data  |(data if sw=01)|
+--------+-----------+---------+---------+--------+---------------+
???(ここではregではなく、opの拡張。これにより以下の様に命令が変わる):
000:ADD, 010:ADC, 101:SUB, 011:SBB, 111:CMP
 */
	case 0x83: // ADD/ADC/SUB/SBB/CMP modR/M, Imm8
		//w-bit 1なのでワード動作、s-bit 0なので即値は byte
		modrm = mem->read8(get_seg_adr(CS, ip));
		subop = modrm >> 3 & 7;
#ifdef CORE_DBG
		prt_post_op(nr_disp_modrm(modrm) + 2);
		printf("%s ", str8x[subop]);
		disas_modrm16(modrm, false, false, true);
		printf("0x%02x\n\n", mem->read8(get_seg_adr(CS, ip + 1)));
#endif
		// xxx SUB DX, Imm8 のみ実装
		switch (subop) {
		case 5: // SUB
			if (modrm >> 6 == 3) {
				genreg16(modrm & 7) -= mem->read8(get_seg_adr(CS, ++ip));
				ip++;
			} else {
				// xxx
			}
		}
		break;

/*
          76  543 210
+--------+-----------+---------+---------+
|100010dw|mod reg r/m|(DISP-LO)|(DISP-HI)|
+--------+-----------+---------+---------+
 */
	case 0x8a: // mov.b reg, modR/M
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
#ifdef CORE_DBG
		prt_post_op(nr_disp_modrm(modrm) + 1);
		printf("MOV ");
		disas_modrm16(modrm, true, true, false);
#endif
		ip++;
		*genreg8[modrm >> 3 & 3] = modrm16b(modrm);
		break;
	case 0x8b: // mov.w reg, modR/M
		modrm = mem->read8(get_seg_adr(CS, ip)); // modR/Mを読み込む
#ifdef CORE_DBG
		prt_post_op(nr_disp_modrm(modrm) + 1);
		printf("MOV ");
		disas_modrm16(modrm, true, true, true);
#endif
		ip++;
		genreg16(modrm >> 3 & 3) = modrm16w(modrm);
		break;

/*
 76543 210
+---------+--------+-------------+
|1011w reg|  data  |(data if w=1)|
+---------+--------+-------------+
 */
	case 0xb0: // mov al, Imm8
	case 0xb1: // mov cl, Imm8
	case 0xb2: // mov dl, Imm8
	case 0xb3: // mov bl, Imm8
	case 0xb4: // mov ah, Imm8
	case 0xb5: // mov ch, Imm8
	case 0xb6: // mov dh, Imm8
	case 0xb7: // mov bh, Imm8
#ifdef CORE_DBG
		prt_post_op(1);
		printf("MOV %s, 0x%02x\n\n", genreg_name[0][op & 7], mem->read8(get_seg_adr(CS, ip)));
#endif
		*genreg8[op & 7] = mem->read8(get_seg_adr(CS, ip++));
		break;
	case 0xb8: // mov ax, Imm16
	case 0xb9: // mov cx, Imm16
	case 0xba: // mov dx, Imm16
	case 0xbb: // mov bx, Imm16
	case 0xbc: // mov sp, Imm16
	case 0xbd: // mov bp, Imm16
	case 0xbe: // mov si, Imm16
	case 0xbf: // mov di, Imm16
#ifdef CORE_DBG
		prt_post_op(2);
		printf("MOV %s, 0x%04x\n\n", genreg_name[1][op & 7], mem->read16(get_seg_adr(CS, ip)));
#endif
		genreg16(op & 7) = mem->read16(get_seg_adr(CS, ip));
		ip += 2;
		break;

	case 0xea: // セグメント外直接ジャンプ
#ifdef CORE_DBG
		prt_post_op(4);
#endif
		warg1 = mem->read16(get_seg_adr(CS, ip));
		warg2 = mem->read16(get_seg_adr(CS, ip + 2));
		update_segreg(CS, warg2);
		ip = warg1;
#ifdef CORE_DBG
		printf("JMP %04x:%04x\n\n", warg2, warg1);
#endif
		break;

/*
+--------+--------+
|1110011w| data-8 |
+--------+--------+
 */
	case 0xe6: // OUT Imm8, AL
#ifdef CORE_DBG
		prt_post_op(1);
		printf("OUT 0x%02x, AL\n\n", mem->read8(get_seg_adr(CS, ip)));
#endif
		io->write8(mem->read8(get_seg_adr(CS, ip++)), al);
		break;
	case 0xe7: // OUT Imm8, AX
#ifdef CORE_DBG
		prt_post_op(1);
		printf("OUT 0x%02x, AX\n\n", mem->read8(get_seg_adr(CS, ip)));
#endif
		io->write16(mem->read8(get_seg_adr(CS, ip++)), ax);
		break;

/*
+--------+
|1110111w|
+--------+
 */
	case 0xee: // OUT DX, AL
#ifdef CORE_DBG
		prt_post_op(0);
		printf("OUT DX, AL\n\n");
#endif
		io->write8(dx, al);
		break;
	case 0xef: // OUT DX, AX
#ifdef CORE_DBG
		prt_post_op(0);
		printf("OUT DX, AX\n\n");
#endif
		io->write16(dx, ax);
		break;
	}
}
