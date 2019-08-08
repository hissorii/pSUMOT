#include "types.h"

namespace pSUMOT {
	class IO;
}

#define SYSROM_SIZE 256*1024

class Memory {
private:
	u8 *ram;
	u8 *sysrom;
	pSUMOT::IO *io;
	Memory *memo;
 public:
	/*-----
	  コンストラクタ・デストラクタは戻り値を取れない [2019-07-28]
	  -----*/
	Memory(u32 size);
	u8 read8(u32 addr);
	void write8(u32 addr, u8 data);
	void setio(pSUMOT::IO *i);
};
