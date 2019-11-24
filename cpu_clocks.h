#define CLK_AAA			4
#define CLK_AAD			19
#define CLK_AAM			17
#define CLK_AAS			4

// ADC, ADD, SUB, SBB, XOR, OR, AND
#define CLK_CAL_R_R		2 // op reg, reg
#define CLK_CAL_R_IMM		2 // op reg, imm
#define CLK_CAL_R_MEM		6 // op reg, mem
#define CLK_CAL_MEM_R		7 // op mem, reg
#define CLK_CAL_MEM_IMM		7 // op mem, imm

#define CLK_CMP_R_R		2
#define CLK_CMP_R_IMM		2
#define CLK_CMP_MEM_R		5
#define CLK_CMP_MEM_IMM		5
#define CLK_CMP_R_MEM		6

#define CLK_CBW			3
#define CLK_CWD			2

#define CLK_DAA			4
#define CLK_DAS			4

#define CLK_INCDEC_R		2
#define CLK_INCDEC_MEM		6

#define CLK_DIV8_R		14
#define CLK_DIV8_MEM		17
#define CLK_DIV16_R		22
#define CLK_DIV16_MEM		25
#define CLK_DIV32_R		38
#define CLK_DIV32_MEM		41

#define CLK_HLT			5

#define CLK_IDIV8_R		19
#define CLK_IDIV8_MEM		22
#define CLK_IDIV16_R		27
#define CLK_IDIV16_MEM		30
#define CLK_IDIV32_R		43
#define CLK_IDIV32_MEM		46

#define CLK_IN			12
#define CLK_PM_IN		26 // xxx if CPL <= IOPL
#define CLK_IN_DX		13
#define CLK_PM_IN_DX		27 

#define CLK_OUT			10
#define CLK_PM_OUT		24 
#define CLK_OUT_DX		11
#define CLK_PM_OUT_DX		25 

#define CLK_INT			37 // xxx
#define CLK_INT3		33 
#define CLK_INTO_OF0		3
#define CLK_INTO_OF1		35 
#define CLK_IRET		22 

#define CLK_LDS			7
#define CLK_PM_LDS		22
#define CLK_LES			7
#define CLK_PM_LES		22

#define CLK_LEA			2

#define CLK_LGDT_LIDT		11

#define CLK_LOCK		0

#define CLK_MOV_RM_R		2
#define CLK_MOV_R_R		2
#define CLK_MOV_R_MEM		4
#define CLK_MOV_RM_SR		2
#define CLK_MOV_SR_R		2
#define CLK_PM_MOV_SR_R		18
#define CLK_MOV_SR_MEM		5
#define CLK_PM_MOV_SR_MEM	19
#define CLK_MOV_MOF		2
#define CLK_MOV_R_IMM		2
#define CLK_MOV_R_CR		6
#define CLK_MOVSX_R_R		3
#define CLK_MOVSX_R_MEM		6
#define CLK_MOVZX_R_R		3
#define CLK_MOVZX_R_MEM		6

#define CLK_NEG_R		2
#define CLK_NEG_MEM		6

#define CLK_NOP			3

#define CLK_NOT_R		2
#define CLK_NOT_MEM		6

#define CLK_POP_R		4
#define CLK_POP_MEM		5
#define CLK_POP_SR		7
#define CLK_PM_POP_SR		21
#define CLK_POPA		24
#define CLK_POPF		5

#define CLK_PUSH_R		2
#define CLK_PUSH_IMM		2
#define CLK_PUSH_MEM		5
#define CLK_PUSH_SR		2
#define CLK_PUSHA		18
#define CLK_PUSHF		4

#define CLK_RCL_R		9
#define CLK_RCL_MEM		10
#define CLK_RCR_R		9
#define CLK_RCR_MEM		10
#define CLK_ROL_R		3
#define CLK_ROL_MEM		7
#define CLK_ROR_R		3
#define CLK_ROR_MEM		7

#define CLK_SALSHL_R		3
#define CLK_SALSHL_MEM		7
#define CLK_SAR_R		3
#define CLK_SAR_MEM		7
#define CLK_SHR_R		3
#define CLK_SHR_MEM		7

#define CLK_SAHF		3
#define CLK_LAHF		2

#define CLK_CMC			2
#define CLK_CLC			2
#define CLK_STC			2
#define CLK_CLI			3
#define CLK_STI			3
#define CLK_CLD			2
#define CLK_STD			2

#define CLK_TEST_R_IMM		2
#define CLK_TEST_MEM_IMM	5
#define CLK_TEST_R_R		2
#define CLK_TEST_MEM_R		5

#define CLK_XCHG_R_R		2

#define CLK_XLAT		5

#define CLK_MOVS		7
#define CLK_CMPS		10
#define CLK_STOS		4
#define CLK_LODS		5
#define CLK_SCAS		7


// xxx Jcc, CALL, RET, INT, LOOP, JMP, MUL
