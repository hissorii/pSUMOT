#include <cstdlib> // for malloc(), size_t
#include "io.h"

/*-----
  xxx *.cppにはusing namespace hoge;ではなくてnamespace hoge {}を使う?
  [2019-07-29]
  -----*/

//using namespace pSUMOT;

namespace pSUMOT {

IO::IO(u32 size) {
	io = (u8 *)malloc((size_t)size);
}
u8 IO::read8(u32 addr) {
	return *(io + addr);
}

} // namespace pSUMOT
