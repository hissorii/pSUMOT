#include <cstdio> // for printf()
#include <fstream>
#include "cdc.h"
#include "event.h"

#define STATUS 0x20
#define IRQ 0x40
#define SIRQ 0x80
#define SRQ 0x01

u8 CDC::srq_count;
CDC::msf CDC::start_msf;

CDC::CDC(void) {
	cdc = this;
}

u8 CDC::read8(u32 addr) {
	u8 ret;
	switch (addr & 0xf) {
	case 0x0: // マスターステータスを返す
		return master_status | (srq_count > 0 ? SRQ : 0);
	case 0x2: // ステータスレジスタ読み込み
		ret = status[status_idx++];
		if (status_idx > 3) {
			status_idx = 0;
			// 全部読んだらSRQのカウンタをデクリメントする
			// コマンド書込み、セクタ読み込みが連続で発生した場合に
			// 二度SRQをセットしたいのでカウンタを使って制御する
			srq_count--;
		}
		// status[0]: まだ読み込み中(0x22), 読み込み終了(0x6)
		return ret;
	}
	return 0;
}

void CDC::write8(u32 addr, u8 data) {
	switch (addr & 0xf) {
	case 0x0: // マスターコントロール
	case 0x2: // コマンドレジスタ書き込み
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
			  +------------+------------+
			  | 開始セクタM|            |
			  +------------+------------+
			  | 開始セクタS|            |
			  +------------+------------+
			  | 開始セクタF|            |
			  +------------+------------+
			  | 終了セクタM|            |
			  +------------+------------+
			  | 終了セクタS|            |
			  +------------+------------+
			  | 終了セクタF|            |
			  +------------+------------+
			  |            |            |
			  +------------+------------+
			  |            |            |
			  +------------+------------+
			 */

//			read_msf.minute = start_msf.minute = parameter[0];
//			read_msf.second = start_msf.second = parameter[1];
//			read_msf.frame  = start_msf.frame  = parameter[2];
//			end_msf.minute = parameter[3];
//			end_msf.second = parameter[4];
//			end_msf.frame = parameter[5];
//
			// CD-ROM読み込み。読込み時間分結果を遅延させたいが
			// 読込みは一瞬で終るので読込みそのものを遅延させる
			ev->add(300, this->read_1sector);

			// 読み込みが終わったらDMAへ転送リクエストを出す
			// xxx DMA転送終る(バッファが空になる)まで次を読まない?
			// xxx 読込み成功したらstatusを0にする
			for (int i = 0; i < 4; i++) {
				status[i] = 0;
			}
			break;
		}
		if (data & STATUS) { // コマンドステータスの要求あり？
			// xxx これは1セクタ読んだ後でセットする
			// xxx いや、コマンド完了自体のステータスも返す
			srq_count++;
		}
		if (data & IRQ) { //コマンドステータス要求時のIRQ制御
			//INTXXX
			master_status |= SIRQ;
		}
		break;
	case 0x4: // パラメータレジスタ書き込み
		parameter[param_idx++] = data;
		if (param_idx > 7) {
			param_idx = 0;
		}
		break;
	}
}

void CDC::read_1sector(void) {
	printf("read_1sector()\n");
	srq_count++;
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

