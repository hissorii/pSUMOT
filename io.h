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
};

} // namespace pSUMOT
