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
};
