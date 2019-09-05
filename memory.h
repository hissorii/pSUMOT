#include "types.h"
#include "bus.h"

#define SYSROM_SIZE 256*1024

class Memory : public BUS {
private:
	u8 *ram;
	u8 *sysrom;
	Memory *memo;
 public:
	/*-----
	  コンストラクタ・デストラクタは戻り値を取れない [2019-07-28]
	  -----*/
	Memory(u32 size);
	u8 read8(u32 addr);
	void write8(u32 addr, u8 data);
	u16 read16(u32 addr);
	void write16(u32 addr, u16 data);
	u32 read32(u32 addr);
	void write32(u32 addr, u32 data);
};
