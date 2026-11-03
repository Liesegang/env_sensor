# Waveshare 3.6inch e-Paper (E) / Spectra 6

Zephyr用の表示ドライバー。nativeの画面サイズは400×600、6色、4bpp。
1バイトの上位4bitが左画素、下位4bitが右画素になる。
色コードは黒0、白1、黄2、赤3、青5、緑6。

初期化値、リセットのタイミング、表示更新の順序は提供されたCardサンプルを
参照して移植した。Arduino・ESP32・サンプルのソースやビルドには依存しない。
`epaper_sample`を削除しても、このディレクトリだけでビルドできる。
元のWaveshareドライバーのMIT許諾文は`LICENSE`に保持している。

## アプリから使う

アプリのCMakeからこのディレクトリを`add_subdirectory`で取り込み、
`#include <epd3in6e.h>`する。`struct epd3in6e`に以下を指定する。

- `spi`: mode 0、MSB first、8bit、最大4MHz。`spi_dt_spec`のCSを使用する。
- `dc`: 物理Highがデータ、物理Lowがコマンド。
- `reset`: `GPIO_ACTIVE_LOW`。
- `busy`: `GPIO_ACTIVE_LOW`。物理LowがBUSY、Highがidle。

ボード側の3.3V電源を安定させてから`epd3in6e_init()`を呼ぶ。
ピン番号や電源制御はアプリが持ち、同じドライバーを別のボードからも使える。

```c
static int white_row(void *context, uint16_t y, uint8_t *row)
{
    ARG_UNUSED(context);
    ARG_UNUSED(y);
    memset(row, 0x11, EPD3IN6E_ROW_BYTES);
    return 0;
}

int err = epd3in6e_init(&display);
if (err == 0) {
    err = epd3in6e_refresh(&display, white_row, NULL);
}
if (err == 0) {
    err = epd3in6e_sleep(&display);
}
```

`epd3in6e_refresh()`は200byteの行バッファだけで120,000byteの画像を送信する。
コールバックは行0〜599の順で呼ばれ、各行の200byte全てを埋める。
SPIバスとCSは各転送の終了時に解放するため、別CSのSDカードと共用できる。
120,000byteの画像を既に持つ場合は`epd3in6e_refresh_frame()`でも送信できる。
画面の回転・文字・画像の生成は呼び出し側で行う。

## 完了とエラー

各BUSY待ちは最大60秒。power-onとrefreshではBUSYの物理Low→Highを確認し、
2秒以内にLowを検出しなければ`-ENODEV`、Lowが続けば`-ETIMEDOUT`を返す。
SPI成功だけをもって表示完了とはしない。成功はBUSY応答の確認であり、
表示内容・色・画素の向きは実機での目視確認が必要。

正常なrefreshの最後には内部のPOWER_OFFを送る。sleepは追加で0x07/A5を送り、
ボード側の3.3V電源は操作しない。sleepやrefreshエラー後は再度initが必要。
POWER_ON以降にエラーが出た場合は、最大3秒のPOWER_OFFとdeep sleepを試みて、
元のエラーを返す。

関数は同期処理で、同じ`struct epd3in6e`への並行呼び出しは呼び出し側で防ぐ。
このライブラリはZephyrの汎用`display`デバイスAPIには登録しない。

## 参照元

- 提供されたCardサンプルのreset/init/image/refresh処理（移植時の参照のみ）
- [Waveshare EPD_3in6e.c（V1.0）](https://github.com/waveshareteam/e-Paper/blob/a794fbc39656b0f93938d1ffb3fdc77eaed9e9fc/E-paper_Separate_Program/3.6inch_e-Paper_E/RaspberryPi_JetsonNano/c/lib/e-Paper/EPD_3in6e.c)

初期化は13コマンド、解像度レジスターは0x61 / 01 90 02 58。
表示更新は0x04 → 0x06 / 6F 1F 16 29 → 0x12 / 00 → 0x02 / 00。
