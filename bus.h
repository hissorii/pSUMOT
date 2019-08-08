#pragma once

#include "types.h"

class BUS {
/*-----
  [2019-08-08]
  基底クラスのprivate変数を子クラスから参照したい場合はprotectedを使う
  -----*/
protected:
	static BUS *mem;
	static BUS *io;
public:
	/*-----
	  抽象仮想関数 [2019-08-08]
	  -----*/
	virtual u8 read8(u32 addr) = 0;
	virtual void write8(u32 addr, u8 data) = 0;
};

