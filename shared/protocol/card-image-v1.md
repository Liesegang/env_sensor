# Card image transfer v1

Cardの電子ペーパー画像を転送する専用BLEサービス。環境センサーのOE/OF形式とは別に扱う。
認証付きLE Secure Connections（L4）と既存のbondを使用し、全整数はlittle endian。

| 用途 | UUID | 操作 |
| --- | --- | --- |
| Service | `722acd90-e77f-468d-a916-67e599e7cffd` | |
| Control | `722acd91-e77f-468d-a916-67e599e7cffd` | Write with response |
| Data | `722acd92-e77f-468d-a916-67e599e7cffd` | Write with response |
| Status | `722acd93-e77f-468d-a916-67e599e7cffd` | Read / Notify |

## 画像

ネイティブ400×600、4bpp、120,000 bytes。1バイトの上位ニブルが左、下位が右のピクセル。
色コードは黒0、白1、黄2、赤3、青5、緑6。その他のニブルは拒否する。
アプリは600×400の横向き画像へ縮小・余白追加・6色Floyd–Steinbergディザを適用する。
横向き座標 `(x,y)` はネイティブ `(399-y,x)` へ変換する。

CRCはIEEE CRC-32（zlibと同じ、初期値0）。サイズ、色コード、CRCが一致した画像だけ保存・表示する。

## Control

BEGINは20 bytes。`IC`、version=1、operation=1に続けて、transfer ID（u32、offset 4）、
length（u32、8）、CRC（u32、12）、width（u16、16）、height（u16、18）。IDは0以外。
COMMIT / ABORTは8 bytes。同じヘッダーでoperation=2 / 3、offset 4に同じIDを置く。

## Data

`ID`、version=1、reserved=0、transfer ID（u32、offset 4）、画像内offset（u32、8）、
画像bytes（offset 12）。payloadは最大232 bytes、かつ実際のATT書込上限から12を引いた範囲。
offsetは順番通り。直前のchunkを同じID・offset・bytesで再送した場合だけ重複として受理する。

Write応答は処理待ちへの受理を示す。保存済み位置はStatusで確認し、
`received >= chunk offset + chunk length`となってから次のchunkを送る。

## Status

24 bytes: `IS`、version=1、state（byte 3）、transfer ID（u32、4）、received（u32、8）、
total（u32、12）、CRC（u32、16）、error（signed i32、20）。

| State | 意味 |
| --- | --- |
| 0 | Idle |
| 1 | Receiving。次のDataを受理できる |
| 2 | Stored。CRC検証とSD保存が完了 |
| 3 | Error |
| 4 | Displaying |
| 5 | Displayed。パネル更新が完了 |
| 6 | Preparing。BEGIN処理待ち |
| 7 | Committing。COMMIT処理待ち |

BEGIN後はstate 1、各Data後は保存位置、COMMIT後はstate 5または3を待つ。
アプリはCOMMIT送信後にキャンセルを行わない。ファームウェアはディスク再読CRC検証後、
commit markerを書き込む直前に取消不能な境界を設ける。それ以降はABORTを拒否し、
接続が切れても画像の確定・表示を続ける。境界より前の取消は以前の保存画像を維持する。
転送・パネル更新中はBLE接続期限を延長する。未確定の画像や失敗した画像は表示しない。
新旧画像をSDの2スロットへ保存し、起動時にcommit markerとCRCを検証して最新の有効画像を選ぶ。
転送バッファとパネル描画バッファを別々に120KB確保しない。
