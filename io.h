#include "types.h"
#include "bus.h"

namespace pSUMOT {

class IO : public BUS {
private:
	u8 *iop; // I/O port
public:
	IO(u32 size);
	u8 read8(u32 addr);
	void write8(u32 addr, u8 data);
	u16 read16(u32 addr);
	void write16(u32 addr, u16 data);
	u32 read32(u32 addr);
	void write32(u32 addr, u32 data);

};

} // namespace pSUMOT
