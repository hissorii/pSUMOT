#include <iostream>
#include "cpu.h"

CPU::CPU(BUS* bus) {
	mem = bus->get_bus("mem");
	io = bus->get_bus("io");
}

void CPU::dump_reg() {
	std::cout << "eax" << '\n';
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
void CPU::exec() {
	u8 op;
	u8 arg1, arg2;
	u16 warg1, warg2;
	int i;

	dump_reg();
	op = mem->read8(get_seg_adr(CS, ip));

	std::cout.width(8);
	std::cout.fill('0');
	std::cout << std::hex << get_seg_adr(CS, ip) << '\t' << (int)op;
	ip++;

	switch (op) {
	case 0x00:
		break;
	case 0x8b: // mov.w reg, X
		for (i = 0; i < 3; i++)
			std::cout << ' '
				  << (int)mem->read8(get_seg_adr(CS, ip + i));
		arg1 = mem->read8(get_seg_adr(CS, ip));
		std::cout << '\t' << "MOV " << genreg_name16[arg1 >> 3 & 3] << ", " << '\n';

		break;
	case 0xea: // セグメント外直接ジャンプ
		for (i = 0; i < 4; i++)
			std::cout << ' '
				  << (int)mem->read8(get_seg_adr(CS, ip + i));
		warg1 = mem->read16(get_seg_adr(CS, ip));
		warg2 = mem->read16(get_seg_adr(CS, ip + 2));
		update_segreg(CS, warg2);
		ip = warg1;

		std::cout << '\t' << "JMP " << warg2 << ':' << warg1 << '\n';

		break;
	}
}
