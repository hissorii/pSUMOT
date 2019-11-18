#include <cstdio> // for printf()
#include <fstream>
#include "cdc.h"

#define STATUS 0x20
#define IRQ 0x40
#define SRQ 0x01


CDC::CDC(void) {
	cdc = this;
}

u8 CDC::read8(u32 addr) {
	u8 ret;
	switch (addr & 0xf) {
	case 0x2: // ステータスレジスタ読み込み
		ret = status[status_idx++];
		if (status_idx > 3) {
			status_idx = 0;
		}
		// status[0]: まだ読み込み中(0x22), 読み込み終了(0x6)
		return ret;
	}
	return 0;
}

void CDC::write8(u32 addr, u8 data) {
	switch (addr & 0xf) {
	case 2: // コマンドレジスタ書き込み
		switch (data & 0x1f) {
		case 0x0: // xxx ready?
			//nothing to do
			break;
		case 0x2: // xxx CD-ROM read?
			/*
			  CD-ROM読み込み時のパラメーター
			  M:minute, S:second, F:frame
			  (うんづ互換ROMのFMT_SYS.ROMの解析による
			  http://townsemu.world.coocan.jp/compatiroms.html)
			  CD-ROMモード1の1セクタは2352バイト
			  (FM TOWNS テクニカルデータブック p220)
			  +------------+
			  |            |
			  +------------+
			  |            |
			  +------------+
			  | 開始セクタF|
			  +------------+
			  | 開始セクタS|
			  +------------+
			  | 開始セクタM|
			  +------------+
			  | 終了セクタF|
			  +------------+
			  | 終了セクタS|
			  +------------+
			  | 終了セクタM|
			  +------------+
			 */

			// CD-ROM読み込み。読み込み時間分、結果は遅延させる？
			// 読み込みが終わったらDMAへ転送リクエストを出す
			break;

		}
		if (data & STATUS) { // コマンドステータスの要求あり？
			master_status |= SRQ;
		}
		if (data & IRQ) { //コマンドステータス要求時のIRQ制御
			//INTXXX
		}
		break;
	case 0x4: // パラメータレジスタ書き込み
		parameter[param_idx++] = data;
		if (param_idx > 8) {
			param_idx = 0;
		}
		break;
	}
}

/*
u16 CDC::read16(u32 addr) {
	return 0;
}
void CDC::write16(u32 addr, u16 data) {
}
u32 CDC::read32(u32 addr) {
	return 0;
}
void CDC::write32(u32 addr, u32 data) {
}
*/

