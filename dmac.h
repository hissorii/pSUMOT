#include "types.h"
#include "bus.h"

#define DMA_CHANNEL 4

class DMAC : public BUS {
private:
	union dmac_count {
		u16 count16;
		struct {
#ifdef BIG_ENDIAN
			u8 upper8;
			u8 lower8;
#else
			u8 lower8;
			u8 upper8;
#endif
		} count8;
	} basecount[DMA_CHANNEL], curcount[DMA_CHANNEL];

        union dmac_addr {
                u32 addr32;
                struct {
#ifdef BIG_ENDIAN
                        u16 upper16;
                        u16 lower16;
#else
                        u16 lower16;
                        u16 upper16;
#endif
      		} addr16;
                struct {
#ifdef BIG_ENDIAN
                        u8 a3;
                        u8 a2;
                        u8 a1;
			u8 a0;
#else
                        u8 a0;
                        u8 a1;
                        u8 a2;
                        u8 a3;
#endif
                } addr8;
        } baseaddr[DMA_CHANNEL], curaddr[DMA_CHANNEL];

	u8 channel; // write時のフォーマットで保持する
	char selch[DMA_CHANNEL] = {1, 2, 4, 8};
	u16 devctrl; // デバイスコントロールレジスタ
	u8 modectrl; // モードコントロールレジスタ
	u8 mask; // マスクレジスタ
public:
	DMAC(void);
	void dmareq(u8 channel);
	u8 read8(u32 addr);
	void write8(u32 addr, u8 data);
	u16 read16(u32 addr);
	void write16(u32 addr, u16 data);
//	u32 read32(u32 addr);
//	void write32(u32 addr, u32 data);

	bool working;
};
