#pragma once
#include "types.h"

class Event;

class BUS {
/*-----
  [2019-08-08]
  基底クラスのprivate変数を子クラスから参照したい場合はprotectedを使う
  -----*/
protected:
	/*-----
	  静的メンバ [2019-08-10]
	  -----*/
	static BUS *mem;
	static BUS *io;
	static BUS *dmac;
	static BUS *cdc;
	static Event *ev;
public:
	/*-----
	  抽象仮想関数 [2019-08-08]
	  -----*/
	virtual u8 read8(u32 addr) = 0;
	virtual void write8(u32 addr, u8 data) = 0;
	virtual u16 read16(u32 addr) = 0;
	virtual void write16(u32 addr, u16 data) = 0;
	virtual u32 read32(u32 addr) = 0;
	virtual void write32(u32 addr, u32 data) = 0;

	BUS* get_bus(const char *s);
	void set_ev(Event *ev);
};
