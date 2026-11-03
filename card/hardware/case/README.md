# Card env sensor v2 ケース

FreeCADで作成した、FDM・PETG用の前後2分割ケース。
前から「前枠、裸のe-Paper、低頭ねじ、基板と電池、後ケース、外側ナット」の順で重なる。
基板はM3ねじと背面の外側から入れるナットで後ケースに固定し、前後ケースは4個のスナップで閉じる。

## 確認するファイル

- `generated/card_env_sensor_case_assembly.FCStd`: ケースと実装部品、電池、パネル、FPC、固定パッドを組み立てたFreeCADモデル。
- `generated/card_env_sensor_case_draft.FCStd`: 前後ケースの2ソリッド。
- `generated/card_env_sensor_case_assembly.step`: 組立参照STEP。
- `generated/card_env_sensor_v2_populated.FCStd` / `card_env_sensor_v2_populated.step`: ケース設計に使った、PCBと93個の部品参照だけのモデル。
- `generated/card_env_sensor_case_front_draft.step` / `card_env_sensor_case_rear_draft.step`: 前後ケースの個別STEP。
- `generated/card_env_sensor_case_front_print.stl` / `card_env_sensor_case_rear_print.stl`: 印刷向きに置いたSTL。単位mm。
- `generated/case_preview.png` / `case_exploded.png`: 実CADから描画した確認画像。
- `generated/fit_report.json`: 部品、最大寸法、操作範囲、配線・FPCとケースの干渉検査。

`build_case_draft.py`、`build_stack_reference.py`の寸法を変更して再生成できる。
FreeCADモデルの各参照部品には形状の根拠と仮定を記録した。

## 外形と固定

外形は91.0 × 60.4 mm。主面の厚さは7.65 mm。
電池コネクタを覆う局所部分は9.75 mm、USB上部は8.05 mm。
SW1の操作部は局所的にケースから出るため、操作部まで含めると8.65 mm。
PH系横向き電池コネクタの高さ約4.8 mmを覆うため、ケース全面を7 mmに揃えることはできない。

前枠の表示開口は76.8 × 51.4 mm。裸パネルの表示領域76.2 × 50.8 mmに、各辺0.3 mmの余裕を加えた。
前面のリップは0.4 mm、後ケースの主面は0.8 mm、USB直上の局所壁は0.6 mm。
前後ケースが閉じる座面を設け、スナップを押し込んでパネルを過度に圧縮しない構造とした。

4個のスナップは、前枠の厚さ0.9 mm・長さ11.0〜11.5 mmの弾性舌と、後ケースのフックで組になる。
舌の切り離し溝は0.6 mm、掛かりは0.3 mm、爪の肩厚は0.4 mm、受け上部は0.45 mm。
閉じ位置での軸方向の余裕は0.05 mm。PETGの実際の嵌合はプリンタの寸法差を含むため、初回は試作として確認する。

パネルの非表示縁4箇所に、厚さ0.05 mmの薄い両面テープを置く。
後ケースの4個の保持棚には厚さ0.30 mmのシリコーンフォームを貼る。
パネル厚0.78±0.07 mmに対して、フォームの閉じた厚さは0.15〜0.29 mmになる。
これでパネルの前後の遊びを吸収する。

電池はPCBの26.0 × 36.5 mmの切り欠きに入れる。
背面に15 × 20 × 0.30 mmの両面固定パッドを置き、XY方向と厚さ方向のカタつきを止める。
パッドは電池の配線出口から離し、電池袋を硬い突起で押す構造にしない。

## 配線、操作、空気の通り道

電池の線は切り欠き上端からCN1の挿入口まで短く通す。
モデルは線束径1.2 mm、溝は半径0.7 mmで、線のまわりだけに0.1 mmの逃げを加えた。
CN1の嵌合プラグも局所カバー内に含めた。

パネルのFPCは左側のPCB切り欠きからJ1へ折り返す。
幅25.5 mm、中心の偏り1.31 mm、折り返し部と先端の厚さを含む包絡を置いた。
左側の後ケース壁を局所的に逃がし、前枠の外壁がその外側を覆う。
FPC根元のUV接着部は、図面のモジュール最大厚1.20 mmで確認した。
FPCの0.18 mm厚・R0.6の二段折り返しは配置用の仮定であり、実物の折り返しに合わせて確認する。

USB-C、microSD、SW1、S1の開口は前後の壁の両方へ通してある。
USBは外縁1.3 mmの範囲だけ幅11.5 × 高さ5.7 mmの凹みを設け、ケーブルの樹脂部が奥の端子へ届くようにした。
この樹脂部の寸法は配置用の仮定で、使用するケーブルの形状によっては調整が必要。
S1はメーカーSTEPの天地と原点を補正し、レバー移動と押し込みの範囲にも逃げを設けた。
microSDはロック状態から排出までのカード突出を含めて確認した。
IC5の無線モジュール周囲には樹脂との距離1.5 mm以上を確保する局所逃げを設けた。
右下のスナップ爪を少し下へ寄せ、前枠の局所壁厚0.775 mmを残して外形を維持した。

後ケースには幅1.1 mm・長さ20 mmのセンサースリットを4本と、下端の吸気開口を設けた。
SGP41、BME690、SHT45、STCC4を含む実装位置に合わせた。
照度センサーの穴とブザーの穴も設けている。SFA40は今回の実機では未実装のため含めない。

## 組立部材と印刷

必要な部材はケース前後、FLA-0305E M3×5を3本、M3六角ナットを3個、上述の固定材。
ナットは二面幅5.5 mm・厚さ2.4 mmで、ケースのポケットは二面幅5.7 mm。
ナットの六角座は背面側へ開口し、外側から「ナット、後ケース、基板、ねじ」の順になる。
基板に接する支持面からナット座までのケース厚は1.0 mm、ねじの貫通穴は直径3.4 mm。
ナット座の外周ボスは直径8.2 mmで、六角穴の角の周りにも約0.81 mmの肉厚を残す。
M3×5の長さから基板1.6 mmとケース1.0 mmを引いた、公称2.4 mmがナットへのねじ掛かりになる。
ナット外面とねじ先端は主背面より0.4 mm奥へ収まり、六角座でナットの回転を止める。
低頭ねじは頭径6 mm・高さ0.7±0.15 mmでモデル化した。
パネル最大厚とねじ頭最大高さを組み合わせた残りの隙間は0.10 mm。

印刷後に前枠の切り離しブリッジ4箇所を切り、スナップ舌を動かしてから組み立てる。
後ケースへ電池用パッドを貼り、基板・電池・配線を位置決めする。
背面側の六角座へナットを入れて保持し、パネルを基板から少し離して基板側からねじを締める。
FPCを左側の逃げに沿わせる。
パネルの縁を前枠へ固定し、4個の背面保持パッドを確認して前後をスナップで閉じる。

想定は0.4 mmノズル・0.2 mm積層・PETG。
前枠STLは表示面のリップがベッド側。
スナップ舌の自由端に厚さ0.2 mmの切り離しブリッジを4箇所設け、舌の最初の層を両端で支持する。
印刷後は各ブリッジを薄い刃で切り、0.6 mmの溝を通して舌が動くことを確認する。
舌下面の11〜11.5 mmのブリッジ品質を確認し、必要な場合だけ局所サポートを使う。
後ケースSTLは背面を下にし、CN1カバーの最高部がベッドに触れる。
主背面がベッドから2.1 mm離れるため、その下にサポートが必要。
保持棚の下面には局所サポートを付け、パッド接触面には付けない。
外側の六角座の中には局所サポートを入れ、印刷後に除去してナットの接触面とねじ穴を清掃する。
スナップ受けの位置決め面にはサポートを付けない。

## 検証と未確定条件

PCBは現行v2から外形、中央・左側の切り欠き、取り付け穴、実装穴とビアの310個の穴を読み取った。
33個のライブラリ・フットプリント組のうち、29組に28個のSTEPを用意した。
未実装SFA40類と裸の電源パッド4組を除外し、93参照部品を配置している。
メーカーSTEPがない部品は、図面・フットプリントに基づく寸法包絡で表す。
部品の型番と根拠、確度は `component_envelopes.json` と `fusion_libraries/model_map.json` に記録した。

干渉検査は前後シェル、部品包絡、パネル最大厚、低頭ねじ最大高さ、スイッチ操作、microSD排出、配線、FPC、固定材を含む。
実装スルーホールの12箇所には、PCB裏へ0.5 mm残る足と周囲0.1 mmの包絡も追加した。
279組の検査時の共通体積は0 mm³。S1の位置決めピンとPCBの穴の一致、前後が単一の有効ソリッドであることも確認した。
外側ナット3箇所について、1.0 mmのケース座、直径3.4 mmの貫通穴、背面からナットを入れる通路、公称2.4 mmのねじ掛かりも確認した。
IC5と樹脂の最短距離は前枠1.500 mm、後ケース1.525 mm。
樹脂配置の根拠は `docs/datasheets/ES4L15BA1_DataSheet_V1_0_20260624E.pdf` の33ページ。

CON3の書き込み端子は実装未確認のためパッドで表している。ピンヘッダを実装している場合は背面の追加逃げが必要。
C35の高さは型番未指定のため2.8 mmの包絡、CN1はフットプリントに一致するPH横向きコネクタの包絡を使う。
電池外装と実際のFPC、FDM公差は現物による初回の嵌合確認が必要。

Fusionの3Dパッケージ登録は未完了。
`fusion_libraries/*.lbr` は基板・回路図から復元した部品定義で、STEPは `fusion_libraries/models/` に用意した。
Fusionで各フットプリントへ登録してから基板へ更新する工程が残っている。
従来の `card_env_sensor_v2_fusion.step` は薄い仮部品を含み、ケースの厚み検証には使用していない。
`card_env_sensor_v2_populated.step` では、未実装と明記した部品以外は実装高さを持つ形状へ置き換えた。
元の98個の部品割当から未実装・裸パッド5個を除いた93個が一致し、CON3以外の92個は対応STEPとの配置と体積の一致も確認した。

## 再生成

リポジトリのルートから、以下の順で実行する。

```sh
PYTHONPATH=/Applications/FreeCAD.app/Contents/Resources/lib \
  /Applications/FreeCAD.app/Contents/Resources/bin/python \
  card/hardware/case/build_pcb_model.py
PYTHONPATH=/Applications/FreeCAD.app/Contents/Resources/lib \
  /Applications/FreeCAD.app/Contents/Resources/bin/python \
  card/hardware/case/build_footprint_models.py
PYTHONPATH=/Applications/FreeCAD.app/Contents/Resources/lib \
  /Applications/FreeCAD.app/Contents/Resources/bin/python \
  card/hardware/case/build_stack_reference.py
PYTHONPATH=/Applications/FreeCAD.app/Contents/Resources/lib \
  /Applications/FreeCAD.app/Contents/Resources/bin/python \
  card/hardware/case/build_populated_board.py
PYTHONPATH=/Applications/FreeCAD.app/Contents/Resources/lib \
  /Applications/FreeCAD.app/Contents/Resources/bin/python \
  card/hardware/case/build_case_draft.py
PYTHONPATH=/Applications/FreeCAD.app/Contents/Resources/lib \
  /Applications/FreeCAD.app/Contents/Resources/bin/python \
  card/hardware/case/check_fit.py
PYTHONPATH=/Applications/FreeCAD.app/Contents/Resources/lib \
  /Applications/FreeCAD.app/Contents/Resources/bin/python \
  card/hardware/case/render_case.py --no-singles
```
