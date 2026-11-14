# 既知の問題

[English](KNOWN_ISSUES.md) · この文書は英語版を正とします。日本語版はその訳で、両者が食い違う場合は英語版に従います。

この一覧には、測ったこと、またはコードで読んだことだけを載せています。特に書いていない限り、
測定は 2026-11-14 に、macOS 27.2（arm64）、GCC 16.2.0、CMake 4.4.4、SuiteSparse 7.6.1 で行いました。

## テスト

- **7 件のテストが失敗します。** `tests/known_failures.txt` に並べてあります。
  - `ManufacturerModels.*`（5 件）と `BJTModelTest.SimpleCommonEmitter` は、リポジトリにない
    `tests/models/2N3904.lib` と `tests/models/1N4148.lib` を読み込みます。
  - `CMOS_Inverter.DCTransferCharacteristic_HighInput` は、出力電圧が 0 V 以上であることを
    確かめ、macOS（arm64）と Ubuntu 24.04（arm64）では -4.1e-25 V を得ます。Ubuntu 24.04（x86-64）の
    GCC 13.3.0 と clang 18.1.3 では合格します。
- **比較のテスト 20 件が、常にスキップされます。** テストは参照データを `tests/validation/` の下で
  探しますが（例：`tests/validation/rc_lowpass_ac_ngspice.csv`）、ファイルは `validation/` にあります。
  テストが探す場所にファイルを置くと、20 件のうち 19 件が合格し、
  `MOSFET2StageAmplifierValidation.MatchesNgspice` が失敗します。10 Hz 付近で位相が参照と最大
  11.7 度ずれ、許容は 5 度です。どちらが正しいかは未確認です。
- `validation/` と `tests/validation/` の参照データを作った ngspice の版は記録されていません。
- `BSIM4TemperatureValidationTest.TemperatureSweepValidation` はスキップされます。
  `scripts/bench/generate_bsim4_temperature_refs.py` が書き出す `/tmp/bsim4_temperature_refs.json` を
  読むためです。
- `BSIM4ComprehensiveTest.NgspiceComparison` は `ngspice` を実行し、ngspice が入っていなければ
  スキップされます。macOS で Homebrew の ngspice-47 を入れると、実行して合格します。
- テストの一部は、ビルドディレクトリからの相対位置でデータを探すので、ビルドディレクトリが
  ソースの直下の `build/` のときだけ合格します。

## ネットリストの入力

[ユーザーマニュアル](docs/USER_MANUAL.ja.md)と[リファレンスマニュアル](docs/REFERENCE_MANUAL.ja.md)を
書くときに、コミット `91af284` からビルドしたプログラムを動かして見つけたものです。この節で SPICE と
ngspice について書いていることは、それぞれのマニュアルによります。ngspice との 5 つの違い（倍率、AC 解析の
入力、電源の電流、入れ子の `.DC`、`gnd`）は、ngspice-47 を動かして確かめ、
[NETLIST_CONVENTIONS.md](docs/spec/NETLIST_CONVENTIONS.ja.md) のとおり ngspice に合わせてなくしました。
その文書には、そこで決めていない違いも挙げてあります。

- **`B` 電源には AC のモデルと `I(<電源>)` がありません。** AC 解析では `B` 電源は何も寄与せず、
  `AC analysis not yet supported` をログに出します。`I(...)` を含む式はエラーです。
- **波かっこの中と `.OPTIONS` の値では、倍率を受け付けません**（`.OPTIONS` では警告を出してその
  オプションを無視します）。
- **`.SUBCKT` の中で展開するのは、`R`、`C`、`L`、`V` と `I`（DC の値）、`M`（4 端子、レベル 1 または
  BSIM4）、`X` だけです。** ほかの素子は警告を出して飛ばします。`PARAMS:` は受け付けません。
- **サブ回路は、グローバルの `.PARAM` を読めません。** サブ回路の本体の中括弧から見えるのは、そのサブ回路
  自身のパラメータだけです。最上位に `.PARAM RX=5k` があっても、`.SUBCKT` の中の `R1 a 0 {RX}` は
  `Undefined symbol: 'RX'` で実行を止めます。ngspice-47 は 5 kΩ を使います。値を `X` の行で渡せば使えます
  （`.SUBCKT S a RX=1k` と `X1 in S RX={RXG}`）。逆に、`.SUBCKT` の中に書いた `.PARAM` はグローバルに
  なり、最上位からは読めて、そのサブ回路からは読めません。ngspice-47 ではサブ回路の中だけで効きます
  （コミット `8b1582c` で測りました）。
- **`.FUNC` は素子の値の中では呼べず、** `.PARAM` の式の中だけで呼べます（`Expression parse error`）。
- **`.IF` は `.PARAM` の値を読めません。** 条件を評価できないと、パーサーは警告を出して、すべての
  分岐を残します。
- **同じ名前の 2 つ目の `.PARAM` は無視されます。** 警告 `Failed to add ... to symbol table` が出ます。
- **`.INC` はメッセージなしで無視されます。** 読むのは省略しない `.INCLUDE` だけです。`.inc` で抵抗を
  取り込むネットリストは `V(out)` = 1 V になりますが、`.include` と、ngspice-47 の `.inc` では 0.5 V です
  （コミット `ff52eba` で測りました）。
- **`.IC` と `.NODESET` は大文字の `V(` が必要です。** `v(out)=1` は警告を出して飛ばします。
- **波形は `PULSE` だけで、DC の値の後にしか書けません。** `V1 in 0 PULSE(...)` は
  `Invalid number format: missing digits` で止まります。
- **ダイオードが読むのは `IS` と `N` だけ、バイポーラトランジスタが読むのは `IS`、`BF`、`NF`、`ISE`、
  `NE`、`ISC`、`NC` とノイズのパラメーターだけです。レベル 1 の MOSFET は、式にバルク端子がなく、
  `LAMBDA` もなく、`GAMMA` はノイズの係数です。** `.MODEL` のほかのパラメーターは警告なしに
  無視します。知らない BSIM4 のパラメーター名も同じです。
- **`.PRINT`、`.SAVE`、`.STEP`、`.SENS`、`.MC`、`.TF`、`.PZ`、`.DISTO`、`.PORT`、`.WCASE`、`.OPTIMIZE` は
  読みますが、プログラムは使いません。** `.MEASURE` と `.TITLE` は無視します。
- **`.FOUR` は小文字の `v(` か `i(` でなければなりません。** `.FOUR 1k v(in)` は過渡解析の後に高調波と THD を
  表示しますが、`.FOUR 1k V(in)` は `Invalid .FOUR signal format` で実行を止めます。ngspice-47 はどちらも
  読みます（コミット `ff52eba` で測りました）。
- **`.END` の後の行も読みます。**
- **`.control` ブロックの中の解析は、ngspice と同じようには実行しません。** コミット `7434904` で動かすと、
  `.control` の中の `op` では動作点が選ばれず（プログラムは既定の動作点に戻り、電流を表示しません）、
  `dc V1 0 1 0.5 V2 0 1 1` は `V1` だけを掃引しました。解析はドットのディレクティブ（`.op`、`.dc`、
  `.tran`、`.ac`）で書いてください。

## 解析

特に書いていない限り、コミット `91af284` からビルドしたプログラムで測りました。

- **無損失線路（`T`）は、動作点では開放になります。** DC では無損失線路は両端をつなぎます（電圧が等しく、
  電流は大きさが同じで向きが逆）が、DC の行列への書き込みが空です。1 V を 50 Ω を通して
  `T1 a 0 b 0 Z0=50 TD=10n` に入れ、50 Ω で終端すると、`V(a)` = 1 V、`V(b)` = 0 V になります。正しくは
  どちらも 0.5 V です。そのような動作点から始める過渡解析は、その状態から始まります（コミット `5e3008d` で
  測りました）。
- プログラムが使わない、ライブラリの可変刻みの経路では、方法を `TRAPEZOIDAL` に固定すると、固定刻みの
  経路が行う最初の後退オイラー法のステップなしで始まります（コードで読んだだけで、実行していません）。
- **`.TRAN` の `tmax` は効きません。** パーサーは読みますが、プログラムは `tstep` で刻みます。
  `.TRAN 10u 1m 0 2u` は、`.TRAN 10u 1m` と同じく 101 点になります。ngspice-47 は刻みを `tmax` までに
  抑え、508 点、最大の刻み 2 µs になります（コミット `d858190` で測りました）。

## デバイスモデル

- **BSIM4 のノイズは、別の簡略化した電流のモデルを使います。** 動作点、DC、過渡、AC の解析は
  `BSIM4Core`（`src/devices/bsim4_core.cpp`）を通して BSIM4 を評価します。ノイズ源
  （`MOSFET_BSIM4::get_noise_sources`）は、代わりに `src/devices/mosfet_bsim4.cpp` の
  `compute_drain_current` と `compute_threshold_voltage` を使い、gm を数値微分で求めます。
- **BSIM4 の UB の既定値は見直し中です。** 値は 2.5e-21 (m/V)² で、横のコメントは、0.18 µm の
  プロセスのモデルカードに合わせて 1.0e-19 から変えたと書いています。BSIM4 マニュアルの
  既定値と合っているかは未確認です。
- **JFET はネットリストから使えません。** ライブラリには JFET のモデル
  （`include/kirimaspice/devices/jfet.hpp`）がありますが、パーサーに `J` 素子がありません。
- **`.OPTIONS VOLTAGE_LIMITING=0` は効きません。** これは `config.dc.enable_voltage_limiting` を設定しますが、
  どこからも読まれず、ダイオードとトランジスタは常に制限します（コードで読んだだけで、実行していません）。
- **一部のデバイスは、ほかのノードの電圧を履歴に残します（コードで読んだだけで、実行していません）。**
  過渡解析で時点を受け入れるたびに、ソルバーは、要素 0 がグラウンドで要素 k がノード k のベクトルを
  デバイスに渡します（`TransientSolver::accept_timestep_solution`）。コンデンサとレベル 1 の MOSFET は
  そのとおりに読みます。ダイオード（要素 k − 1 を読む `core::safe_get_node_voltage` を通して）、BSIM4、
  FinFET、BSIM1 は要素 k − 1 を読むので、電荷のために記録する電圧は別のノードのものです。これが結果を
  どう変えるかは測っていません。
- **`.TEMP` と `.OPTIONS TEMP` は FinFET に届きません（コードで読んだだけで、実行していません）。**
  `apply_temperature_to_devices`（`src/solvers/temperature_utils.cpp`）は、並べたデバイスの種類の温度を
  設定しますが、温度を持つ FinFET はその中にありません。

## ソルバー

- 動く線形ソルバーは KLU だけです。SuperLU_DIST のバックエンドは仮置きで、指定しても KLU に
  切り替わります。

## コマンドラインと出力

- `--version` オプションはありません。
- **プログラムは、動作点と過渡解析を実行するかどうかを文字列の検索で決めます。** ネットリストの
  ファイルの各行の中で、コメントも含めて `.op`、`.dc`、`.tran` を探すので、`.OPTIONS` の行があると
  動作点を計算し、`.tran` に触れたコメントがあると過渡解析を実行します。
- プログラムが知らないオプションは、何も言わずに無視します。ネットリストは最初の引数でなければ
  なりません。
- `--wav-sample-rate` に数でない値を渡すと、捕まえられない `std::invalid_argument` でプログラムが
  終わります（macOS では終了コード 134）。
- 解析の行がなく、動作点が収束しないネットリストでは、エラーのメッセージを出して終了コード 0 で
  終わります。
- AC 解析の CSV には `# Simulator: kirimaspice v0.27` と書かれます。版がその場所に固定の文字列で
  書いてあります。
- ログの行と結果は、どちらも標準出力に出ます。
- `.TEMP` のもとでの動作点と過渡解析ではファイルを書かず、DC スイープでは SSF を書きません。
- 標準出力の過渡解析の表で、最後の時点が 2 回出ることがあります（`.TRAN 10u 5m` と `.TRAN 1n 30n` で
  見られ、`.TRAN 10u 1m` では見られません）。
- `-o <パス>` を指定すると、AC 解析の CSV のファイル名は、パス全体の最後の点から後ろを除いて
  作られます。そのため、ディレクトリ名に点があると、ファイルが親のディレクトリに書かれます。
- SSF の出力形式の仕様は、このリポジトリにはまだ公開していません。

## プラットフォーム

- macOS（arm64）では、GCC 16.2.0、Homebrew の clang 23.1.4（libc++、OpenMP あり）、Apple clang 21.0.0
  （OpenMP なし）のテストの結果が同じです。
- Ubuntu 24.04（x86-64）では、CI が GCC 13.3.0 と clang 18.1.3（libstdc++）、CMake 3.31.6、
  `libsuitesparse-dev` の SuiteSparse 7.6.1 でビルドとテストを行います。macOS との違いは、
  `CMOS_Inverter.DCTransferCharacteristic_HighInput` が合格することだけです。
- 2026-11-14 に、arm64 の新しい `ubuntu:24.04` のコンテナ（macOS 上の Colima）で、README の手順どおりに
  行いました。GCC 13.3.0、CMake 3.28.3、Ninja 1.11.1、Python 3.12.3、Git 2.43.0、`libsuitesparse-dev`
  1:7.6.1+dfsg-1build1 で、ビルドでき、登録された 1097 件のテストのうち、1066 件が合格、23 件がスキップ、
  1 件が無効で、一覧の 7 件が macOS（arm64）と同じく失敗しました。
- README が最低の版としている CMake 3.25 は試していません。試した中でいちばん古いのは 3.28.3 です。
- Windows は試していません。
