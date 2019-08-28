#include <cstdint>  // for uint?_t

/*-----
このコメントは以下で消せる [2019-07-16]
sed '/\/\*-----/,/-----\*\//d' cpu.h

※ -の数を間違えると全部消えてしまったりするので注意
-----*/
/*-----
* C++ memo [2019-07-16]
  - stdint.hとcstdintの違いは?
    -> std名前空間に属することを除いては同じ
  - 名前空間(namespace)とは?
    -> 同じ変数名でも衝突を回避できる
    namespace A {
      int a;
    }
    namecpace B {
      int a;
    }
    int main() {
      A::a = 10;
      B::a = 20;
    }
  - using namespace hogeを使うとhoge::を省略できる
    namespace A {
      int a;
    }
    namecpace B {
      int a;
    }
    using namespace A;
    int main() {
      a = 10;
      B::a = 20;
    }
  - cstdintで定義されるuint8_tはstd::uint8_tとしてもuint8_tとしても使える
-----*/
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;


#ifdef BIG_ENDIAN
#undef BIG_ENDIAN
#endif
// big endianマシンでは以下を有効にする
//#define BIG_ENDIAN
