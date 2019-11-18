#include "types.h"
#include "bus.h"

class CDC : public BUS {
private:
	u8 buffer[8 * 1024];
	u8 param_idx = 0;
	u8 parameter[8];
	u8 status_idx = 0;
	u8 status[4];
	u8 master_status;
public:
	CDC(void);
	u8 read8(u32 addr);
	void write8(u32 addr, u8 data);
  /*
	u16 read16(u32 addr);
	void write16(u32 addr, u16 data);
	u32 read32(u32 addr);
	void write32(u32 addr, u32 data);
  */
};
