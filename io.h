#include "types.h"

namespace pSUMOT {

class IO {
private:
	u8 *io;
public:
	IO(u32 size);
	u8 read8(u32 addr);
};

} // namespace pSUMOT
