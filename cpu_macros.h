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
	(r ^ s) & (r ^ d) & 0x80?		\
	  flagu8 |= OFSET8 : flagu8 &= ~OFSET8;

#define OF_ADDw(r, s, d)			\
	(r ^ s) & (r ^ d) & 0x8000?		\
	  flagu8 |= OFSET8 : flagu8 &= ~OFSET8;

#define OF_ADDd(r, s, d)			\
	(r ^ s) & (r ^ d) & 0x80000000?		\
	  flagu8 |= OFSET8 : flagu8 &= ~OFSET8;


#define OF_ADCb OF_ADDb
#define OF_ADCw OF_ADDw
#define OF_ADCd OF_ADDd

#define OF_SUBb(r, s, d)			\
	(d ^ r) & (d ^ s) & 0x80?		\
	  flagu8 |= OFSET8 : flagu8 &= ~OFSET8;

#define OF_SUBw(r, s, d)			\
	(d ^ r) & (d ^ s) & 0x8000?		\
	  flagu8 |= OFSET8 : flagu8 &= ~OFSET8;

#define OF_SUBd(r, s, d)			\
	(d ^ r) & (d ^ s) & 0x80000000?		\
	  flagu8 |= OFSET8 : flagu8 &= ~OFSET8;

#define OF_SBBb OF_SUBb
#define OF_SBBw OF_SUBw
#define OF_SBBd OF_SUBd

#define FLAG8b(r, s, d, ANDN, CRY)	\
	flag8 = flag_calb[r ANDN];	\
	flag8 |= (d ^ s ^ r) & AF;

#define FLAG8w(r, s, d, ANDN, CRY)	\
	flag8 = flag_calw[r ANDN];	\
	flag8 |= (d ^ s ^ r) & AF;

#define FLAG8bADD(r, s, d, CRY) FLAG8b(r, s, d, , )
#define FLAG8bADC(r, s, d, CRY) FLAG8b(r, s, d, , )
#define FLAG8bSUB(r, s, d, CRY) FLAG8b(r, s, d, & 0x1ff, )
#define FLAG8bSBB(r, s, d, CRY) FLAG8b(r, s, d, & 0x1ff, )
#define FLAG8wADD(r, s, d, CRY) FLAG8w(r, s, d, , )
#define FLAG8wADC(r, s, d, CRY) FLAG8w(r, s, d, , )
#define FLAG8wSUB(r, s, d, CRY) FLAG8w(r, s, d, & 0x1ffff, )
#define FLAG8wSBB(r, s, d, CRY) FLAG8w(r, s, d, & 0x1ffff, )

#define FLAG8dALL(r, s, d)			\
	flag8 = pflag_cal[r & 0xff];		\
	flag8 |= (r == 0)? ZF : 0;		\
	flag8 |= (r & 0x80000000)? SF : 0;	\
	flag8 |= (d ^ s ^ r) & AF

#define FLAG8dADD(r, s, d, CRY)					\
	FLAG8dALL(r, s, d);					\
	/* CF */						\
	flag8 |= ((d >> 1) + (s >> 1) + (d & s & 1)) >> 31;

// CRYは-1(0xff), 0, 1のいずれか
#define FLAG8dADC(r, s, d, CRY)					\
	FLAG8dALL(r, s, d);					\
	/* CF */						\
	flag8 |= ((d >> 1) + (s >> 1) + (((d & 1) + (s & 1) + (CRY & 1)) >> 1)) >> 31;

#define FLAG8dSUB(r, s, d, CRY)					\
	FLAG8dALL(r, s, d);					\
	/* CF */						\
	flag8 |= ((r >> 1) + (s >> 1) + (r & s & 1)) >> 31;

#define FLAG8dSBB(r, s, d, CRY)					\
	FLAG8dALL(r, s, d);					\
	/* CF */						\
	flag8 |= ((r >> 1) + (s >> 1) + (((r & 1) + (s & 1) + (CRY & 1)) >> 1)) >> 31;

#define OPADD +
#define OPADC +
#define OPSUB -
#define OPSBB -

#define CAL_RM_R(STR, BWD, CRY)				\
	modrm = mem->read8(get_seg_adr(CS, eip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr(#STR" ");				\
	DAS_modrm(modrm, true, false, BWD##word);	\
	eip++;						\
	src = genreg##BWD(modrm >> 3 & 7);		\
	if ((modrm & 0xc0) == 0xc0) {			\
		CLKS(CLK_CAL_R_R);			\
		dst = genreg##BWD(modrm & 7);		\
		res = dst OP##STR src + CRY;		\
		genreg##BWD(modrm & 7) = (BWD##CAST)res;\
	} else {					\
		CLKS(CLK_CAL_MEM_R);			\
		tmpadr = modrm_seg_ea(modrm);		\
		dst = mem->read##BWD(tmpadr);		\
		res = dst OP##STR src + CRY;		\
		mem->write##BWD(tmpadr, (BWD##CAST)res);\
	}						\
	FLAG8##BWD##STR(res, src, dst, CRY);		\
	OF_##STR##BWD(res, src, dst);

#define CAL_R_RM(STR, BWD, CRY)					\
	modrm = mem->read8(get_seg_adr(CS, eip));		\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);		\
	DAS_pr(#STR" ");					\
	DAS_modrm(modrm, true, true, BWD##word);		\
	eip++;							\
	dst = genreg##BWD(modrm >> 3 & 7);			\
	if ((modrm & 0xc0) == 0xc0) {				\
		CLKS(CLK_CAL_R_R);				\
		src = genreg##BWD(modrm & 7);			\
	} else {						\
		CLKS(CLK_CAL_R_MEM);				\
		src = mem->read##BWD(modrm_seg_ea(modrm));	\
	}							\
	res = dst OP##STR src + CRY;				\
	genreg##BWD(modrm >> 3 & 7) = (BWD##CAST)res;		\
	FLAG8##BWD##STR(res, src, dst, CRY);			\
	OF_##STR##BWD(res, src, dst);

/******************** ADD ********************/

#define FLAG_LOGOPb(d)		\
	flag8 = flag_calb[d];	\
	flagu8 &= ~OFSET8;
#define FLAG_LOGOPw(d)		\
	flag8 = flag_calw[d];	\
	flagu8 &= ~OFSET8;
#define FLAG_LOGOPd(d)				\
	flag8 = pflag_cal[d & 0xff];		\
	flag8 |= (d == 0)? ZF : 0;		\
	flag8 |= (d & 0x80000000)? SF : 0;	\
	flagu8 &= ~OFSET8;

// LOGical OPeration (OP r, r/m)
#define LOGOP_R_RM(OP, STR, BWD)				\
	modrm = mem->read8(get_seg_adr(CS, eip));		\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);		\
	DAS_pr(#STR" ");					\
	DAS_modrm(modrm, true, true, BWD##word);		\
	eip++;							\
	greg = modrm >> 3 & 7;					\
	dst = genreg##BWD(greg);				\
	if ((modrm & 0xc0) == 0xc0) {				\
		CLKS(CLK_CAL_R_R);				\
		dst OP##= genreg##BWD(modrm & 7);		\
	} else {						\
		CLKS(CLK_CAL_R_MEM);				\
		dst OP##= mem->read##BWD(modrm_seg_ea(modrm));	\
	}							\
	genreg##BWD(greg) = dst;				\
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
		CLKS(CLK_CAL_R_R);			\
		dst = genreg##BWD(rm);			\
		dst OP##= src;				\
		genreg##BWD(rm) = (BWD##CAST)dst;	\
	} else {					\
		CLKS(CLK_CAL_MEM_R);			\
		tmpadr = modrm_seg_ea(modrm);		\
		dst = mem->read##BWD(tmpadr);		\
		dst OP##= src;				\
		mem->write##BWD(tmpadr, (BWD##CAST)dst);\
	}						\
	FLAG_LOGOP##BWD(dst)

/******************** PUSH ********************/

#define PUSHW0(d)					\
	sp -= 2;					\
	mem->write16((segreg[SS] << 4) + sp, (u16)(d));

#define PUSHD0(d)				\
	esp -= 4;				\
	mem->write32((segreg[SS] << 4) + esp, d);

#define PUSH(d)					\
	sp -= 1;				\
	mem->write8(get_seg_adr(SS, sp), d);

#define PUSHW(d)				\
	sp -= 2;				\
	mem->write16(get_seg_adr(SS, sp), d);

#define PUSHD(d)				\
	esp -= 4;				\
	mem->write32(get_seg_adr(SS, esp), d);

#define PUSHW_GENREG(reg)		\
	DAS_prt_post_op(0);		\
	DAS_pr("PUSH "#reg"\n");	\
	PUSHW(reg)

#define PUSHD_GENREG(reg)		\
	DAS_prt_post_op(0);		\
	DAS_pr("PUSH "#reg"\n");	\
	PUSHD(reg)

#define PUSH_SEG(seg)			\
	DAS_prt_post_op(0);		\
	DAS_pr("PUSH "#seg"\n");	\
	PUSHW(segreg[seg])

/* 2バイト命令 (PUSH FS/GS用) */
#define PUSH_SEG2(seg)			\
	DAS_prt_post_op(1);		\
	DAS_pr("PUSH "#seg"\n");	\
	PUSHW(segreg[seg]);		\
	eip++

/******************** POP ********************/

#define POPW0(d)				\
	d = mem->read16((segreg[SS] << 4) + sp);\
	sp += 2;

#define POPD0(d)				\
	d = mem->read32((segreg[SS] << 4) + esp);\
	esp += 4;

#define POPW(d)					\
	d = mem->read16(get_seg_adr(SS, sp));	\
	sp += 2;

#define POPD(d)					\
	d = mem->read32(get_seg_adr(SS, esp));	\
	esp += 4;

#define POPW_GENREG(reg)		\
	DAS_prt_post_op(0);		\
	DAS_pr("POP "#reg"\n");	\
	POPW(reg)

#define POPD_GENREG(reg)		\
	DAS_prt_post_op(0);		\
	DAS_pr("POP "#reg"\n");	\
	POPD(reg)

#define POP_SEG(seg)			\
	DAS_prt_post_op(0);		\
	DAS_pr("POP "#seg"\n");	\
	POPW(dst);			\
	update_segreg(seg, (u16)dst)

/* 2バイト命令 (PUSH FS/GS用) */
#define POP_SEG2(seg)			\
	DAS_prt_post_op(1);		\
	DAS_pr("POP "#seg"\n");	\
	POPW(dst);			\
	update_segreg(seg, (u16)dst);	\
	eip++

/******************** CMP ********************/

#define CMP_R_RM(BWD)					\
	modrm = mem->read8(get_seg_adr(CS, eip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr("CMP ");					\
	DAS_modrm(modrm, true, true, BWD##word);	\
	eip++;						\
	dst = genreg##BWD(modrm >> 3 & 7);		\
	if ((modrm & 0xc0) == 0xc0) {			\
		CLKS(CLK_CMP_R_R);			\
		src = genreg##BWD(modrm & 7);		\
	} else {					\
		CLKS(CLK_CMP_R_MEM);			\
		src = mem->read##BWD(modrm_seg_ea(modrm));	\
	}						\
	res = dst - src;				\
	FLAG8##BWD##SUB(res, src, dst, );		\
	OF_SUB##BWD(res, src, dst)

#define CMP_RM_R(BWD)					\
	modrm = mem->read8(get_seg_adr(CS, eip));	\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);	\
	DAS_pr("CMP ");					\
	DAS_modrm(modrm, true, false, BWD##word);	\
	rm = modrm & 7;					\
	eip++;						\
	src = genreg##BWD(modrm >> 3 & 7);		\
	if ((modrm & 0xc0) == 0xc0) {			\
		CLKS(CLK_CMP_R_R);			\
		dst = genreg##BWD(rm);			\
		res = dst - src;			\
	} else {					\
		CLKS(CLK_CMP_MEM_R);			\
		tmpadr = modrm_seg_ea(modrm);		\
		dst = mem->read##BWD(tmpadr);		\
		res = dst - src;			\
	}						\
	FLAG8##BWD##SUB(res, src, dst, );		\
	OF_SUB##BWD(res, src, dst)

/******************** INC ********************/

// xxx OFの計算がNP2と違う
#define INC_R16(reg)						\
	DAS_prt_post_op(0);					\
	DAS_pr("INC %s\n", #reg);				\
	dst = reg;						\
	reg++;							\
	flag8 &= CF; /* CF以外はリセット*/			\
	flag8 |= flag_calw[reg];				\
	flag8 |= (dst ^ reg) & AF;				\
	(dst ^ reg) & 0x8000?flagu8 |= OFSET8:flagu8 &= ~OFSET8

#define INC_R32(reg)						\
	DAS_prt_post_op(0);					\
	DAS_pr("INC %s\n", #reg);				\
	dst = reg;						\
	reg++;							\
	flag8 &= CF; /* CF以外はリセット*/			\
	flag8 |= pflag_cal[reg & 0xff];				\
	flag8 |= (reg == 0)? ZF : 0;				\
	flag8 |= (eax & 0x80000000)? SF : 0;			\
	flag8 |= (dst ^ reg) & AF;				\
	(dst ^ reg) & 0x80000000?flagu8 |= OFSET8:flagu8 &= ~OFSET8

/******************** DEC ********************/

// xxx OFの計算がNP2と違う
#define DEC_R16(reg)						\
	DAS_prt_post_op(0);					\
	DAS_pr("DEC %s\n", #reg);				\
	dst = reg;						\
	reg--;							\
	flag8 &= CF; /* CF以外はリセット*/			\
	flag8 |= flag_calw[reg];				\
	flag8 |= (dst ^ reg) & AF;				\
	(dst ^ reg) & 0x8000?flagu8 |= OFSET8:flagu8 &= ~OFSET8

#define DEC_R32(reg)						\
	DAS_prt_post_op(0);					\
	DAS_pr("DEC %s\n", #reg);				\
	dst = reg;						\
	reg--;							\
	flag8 &= CF; /* CF以外はリセット*/			\
	flag8 |= pflag_cal[reg & 0xff]; /* ここが|=なのでFLAG8dALL()は使えない */ \
	flag8 |= (reg == 0)? ZF : 0;				\
	flag8 |= (eax & 0x80000000)? SF : 0;			\
	flag8 |= (dst ^ reg) & AF;				\
	(dst ^ reg) & 0x80000000?flagu8 |= OFSET8:flagu8 &= ~OFSET8

/******************** Jcc ********************/

#define JCCWD(STR, COND)					\
	if (opsize == size16) {					\
		DAS_prt_post_op(3);				\
		dst = mem->read16(get_seg_adr(CS, ++eip));	\
		DAS_pr(#STR" 0x%04x\n", dst);			\
		if (COND) {			   		\
			eip += (s16)(dst + 2);			\
		} else {					\
			eip += 2;				\
		}						\
	} else {						\
		DAS_prt_post_op(5);				\
		dst = mem->read32(get_seg_adr(CS, ++eip));	\
		DAS_pr(#STR" 0x%08x\n", dst);			\
		if (COND) {			   		\
			eip += (s32)(dst + 4);			\
		} else {					\
			eip += 4;				\
		}						\
	}


#define JCC(STR, COND)				\
	DAS_prt_post_op(1);			\
	dst = mem->read8(get_seg_adr(CS, eip));	\
	DAS_pr(#STR" 0x%02x\n", dst);		\
	if (COND) {			   	\
		eip += (s8)dst + 1;		\
	} else {				\
		eip++;				\
	}

/******************** ADD/OR/ADC/SBB/AND/SUB/XOR/CMP ********************/

#define IPINCb 1
#define IPINCw 2
#define IPINCd 4

#define CAL_RM_IM(BWD, BWD2, STR, CAST, CRY)			\
	eip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		CLKS(CLK_CAL_R_IMM);				\
		dst = genreg##BWD(modrm & 7);			\
		src = mem->read##BWD2(get_seg_adr(CS, eip));	\
		res = dst OP##STR src + CRY;			\
		genreg##BWD(modrm & 7) = (CAST)res;		\
	} else {						\
		CLKS(CLK_CAL_MEM_IMM);				\
		tmpadr = modrm_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		src = mem->read##BWD2(get_seg_adr(CS, eip));	\
		res = dst OP##STR src + CRY;			\
		mem->write##BWD(tmpadr, (CAST)res);		\
	}						       	\
	eip += IPINC##BWD2;					\
	FLAG8##BWD##STR(res, src, dst, CRY);			\
	OF_##STR##BWD(res, src, dst);

#define LOGOP_RM_IM(BWD, BWD2, OP)				\
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
	eip += IPINC##BWD2;					\
	FLAG_LOGOP##BWD(dst)

#define CMP_RM_IM(BWD, BWD2)					\
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
	eip += IPINC##BWD2;					\
	FLAG8##BWD##SUB(res, src, dst, );			\
	OF_SUB##BWD(res, src, dst)

/******************** XCHG ********************/

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

/******************** TEST ********************/

#define TEST_RM_R(BWD)						\
	modrm = mem->read8(get_seg_adr(CS, eip));		\
	DAS_prt_post_op(nr_disp_modrm(modrm) + 1);		\
	DAS_pr("TEST ");					\
	DAS_modrm(modrm, true, false, BWD##word);		\
	eip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		CLKS(CLK_TEST_R_R);				\
		dst = genreg##BWD(modrm & 0x07)			\
			& genreg##BWD(modrm >> 3 & 7);		\
	} else {						\
		CLKS(CLK_TEST_MEM_R);				\
		dst = mem->read##BWD(modrm_seg_ea(modrm))	\
			& genreg##BWD(modrm >> 3 & 7);		\
	}							\
	FLAG_LOGOP##BWD(dst)

/******************** Rotate/Shift ********************/

#define ROT_L <<
#define ROT_R >>
// MSB
#define ROT_ANDLb 0x80
#define ROT_ANDLw 0x8000
#define ROT_ANDLd 0x80000000
// LSB
#define ROT_ANDRb 0x01
#define ROT_ANDRw 0x0001
#define ROT_ANDRd 0x00000001
// MSBをLSBに右シフト
#define MSB2LSB_SFTb 7
#define MSB2LSB_SFTw 15
#define MSB2LSB_SFTd 31
// dst or res
#define DorR_L dst
#define DorR_R res

#define ROT_RM(OP, BWD, DIR, CNT, FUNC)					\
	eip++;								\
	if ((modrm & 0xc0) == 0xc0) {					\
		CLKS(CLK_##OP##_R);					\
		dst = genreg##BWD(modrm & 0x07);			\
		FUNC;							\
		genreg##BWD(modrm & 0x07) = (BWD##CAST)res;		\
	} else {							\
		CLKS(CLK_##OP##_MEM);					\
		tmpadr = modrm_seg_ea(modrm);				\
		dst = mem->read##BWD(tmpadr);				\
		FUNC;							\
		mem->write##BWD(tmpadr, (BWD##CAST)res);		\
	}								\
	if (CNT != 0) {							\
		/* SF, ZF, AF, PFは影響を受けない*/			\
		/* CF							\
		   左回転: dst << (CNT - 1) & MSB			\
		   右回転: dst >> (CNT - 1) & LSB */			\
		dst ROT_##DIR (CNT - 1) & ROT_AND##DIR##BWD?		\
			flag8 |= CF : flag8 &= ~CF;			\
		/* OFは1シフトの時影響し、その他の場合は		\
		   未定義だが常に計算する。0シフトは不変。		\
		   左回転: CF ^ MSB(res) -> MSB(dst) ^ MSB-1(dst)	\
		   右回転:                  MSB(res) ^ MSB-1(res) */	\
		flagu8 |= ((DorR_##DIR ^ DorR_##DIR << 1) & ROT_ANDL##BWD) >> MSB2LSB_SFT##BWD << 3; \
	}

#define MSBb 0x80
#define MSBw 0x8000
#define MSBd 0x80000000

#define FLAG8bSALSHL(r, cnt) flag8 = flag_calb[r & 0x1ff]
#define FLAG8wSALSHL(r, cnt) flag8 = flag_calw[r & 0x1ffff]
#define FLAG8dSALSHL(r, cnt)			\
	flag8 = pflag_cal[r & 0xff];		\
	flag8 |= (r == 0)? ZF : 0;		\
	flag8 |= (r & 0x80000000)? SF : 0;	\
	/* CF */				\
	flag8 |= dst >> (cnt - 1) & 1;

#define SFT_SALSHL(BWD, CNT)					\
	eip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		CLKS(CLK_SALSHL_R);				\
		dst = genreg##BWD(modrm & 0x07);		\
		res = dst << CNT;				\
		genreg##BWD(modrm & 0x07) = (BWD##CAST)res;	\
	} else {						\
		CLKS(CLK_SALSHL_MEM);				\
		tmpadr = modrm_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		res = dst << CNT;				\
		mem->write##BWD(tmpadr, (BWD##CAST)res);	\
	}							\
	if (CNT != 0) {						\
		FLAG8##BWD##SALSHL(res, CNT);			\
		flag8 |= AF; /* NP2/NP21に合わせる */		\
		/* OFは1シフトの時影響し、その他の場合は	\
		   未定義だが常に計算する。0シフトは不変。	\
		   CF ^ MSB(res) -> MSB(dst) ^ MSB-1(dst) */	\
		(dst ^ dst << 1) & MSB##BWD?			\
			flagu8 |= OFSET8 : flagu8 &= ~OFSET8;	\
	}

#define FLAG8bSHRSAR(r) flag8 = flag_calb[r]
#define FLAG8wSHRSAR(r) flag8 = flag_calw[r]
#define FLAG8dSHRSAR(r)				\
	flag8 = pflag_cal[r & 0xff];		\
	flag8 |= (r == 0)? ZF : 0;		\
	flag8 |= (r & 0x80000000)? SF : 0;

#define SFT_SHR(BWD, CNT)					\
	eip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		CLKS(CLK_SHR_R);				\
		dst = genreg##BWD(modrm & 0x07);		\
		res = dst >> CNT;				\
		genreg##BWD(modrm & 0x07) = (BWD##CAST)res;	\
	} else {						\
		CLKS(CLK_SHR_MEM);				\
		tmpadr = modrm_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		res = dst >> CNT;				\
		mem->write##BWD(tmpadr, (BWD##CAST)res);	\
	}							\
	if (CNT != 0) {						\
		FLAG8##BWD##SHRSAR(res); /* CFオフ */		\
		flag8 |= dst >> (CNT - 1) & 1; /* CF */		\
		flag8 |= AF; /* NP2/NP21に合わせる */		\
		/* OFは1シフトの時影響し、その他の場合は	\
		   未定義だが常に計算する。0シフトは不変。	\
		   MSB(tempDEST)			*/	\
		(dst & MSB##BWD)?				\
			flagu8 |= OFSET8 : flagu8 &= ~OFSET8;	\
	}

#define SFT_SAR(BWD, CNT)					\
	eip++;							\
	if ((modrm & 0xc0) == 0xc0) {				\
		CLKS(CLK_SAR_R);				\
		dst = genreg##BWD(modrm & 7);			\
		res = dst >> CNT;				\
		if (dst & MSB##BWD) {				\
			res |= sar_bit##BWD[CNT];		\
		}						\
		genreg##BWD(modrm & 7) = (BWD##CAST)res;	\
	} else {						\
		CLKS(CLK_SAR_MEM);				\
		tmpadr = modrm_seg_ea(modrm);			\
		dst = mem->read##BWD(tmpadr);			\
		res = dst >> CNT;				\
		if (dst & MSB##BWD) {				\
			res |= sar_bit##BWD[CNT];		\
		}						\
		mem->write##BWD(tmpadr, (BWD##CAST)res);	\
	}							\
	if (CNT != 0) {						\
		FLAG8##BWD##SHRSAR(res); /* CFオフ */		\
		flag8 |= dst >> (CNT - 1) & 1; /* CF */		\
		flag8 |= AF; /* NP2/NP21に合わせる */		\
		/* OFは1シフトの時影響し、その他の場合は	\
		   未定義だが常に計算する。0シフトは不変。	\
		   常に0				*/	\
		flagu8 &= ~OFSET8;				\
	}

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

/******************** セグメントオーバーライド ********************/

#define SEG_OVRIDE(SEG)							\
	DAS_prt_post_op(0);						\
	DAS_pr("SEG="#SEG"\n");						\
		seg_ovride++;						\
		sdcr[DS].base = sdcr[SEG].base;				\
		sdcr[SS].base = sdcr[SEG].base;				\
		if (seg_ovride >= 8) { /* xxx ここら辺の情報不足*/	\
			/* xxx ソフトウェア例外 */ 			\
		}

/******************** INC/DEC/CALL/JMP/PUSH ********************/

#define INCDEC_RM(BWD, OP)					\
	if ((modrm & 0xc0) == 0xc0) {				\
		CLKS(CLK_INCDEC_R);				\
		dst = genreg##BWD(modrm & 7);			\
		res = dst OP 1;					\
		genreg##BWD(modrm & 7) = (BWD##CAST)res;	\
	} else {						\
		CLKS(CLK_INCDEC_MEM);				\
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
