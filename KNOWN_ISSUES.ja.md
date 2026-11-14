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
- `BSIM4ComprehensiveTest.VdsatBranchSelection` は、常に自分でスキップします。そのコメントには、
  参照の電流 2.1 A は確かめられていないと書いてあります。
- `BSIM4Transient.InverterChain_MatchesNgspiceReference` は無効です（名前に `DISABLED_` があります）。
  そのコメントには、BSIM4 の過渡解析が 1 ns 付近で収束しないと書いてあります（確かめ直していません）。
- コミット `db0fef6` の macOS では、CTest のテストは 1162 件です。1132 件が合格、22 件がスキップ
  （上の比較のテスト 20 件、`TemperatureSweepValidation`、`VdsatBranchSelection`）、7 件が失敗、
  1 件が無効です。
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
- **`B` 電源の `TIME` は常に 0 です**（コミット `db0fef6` で）。`B1 out 0 V={2*V(in)+1000*TIME}` で
  `V(in)` = 1 V のとき、`.tran 0.1m 1m` のどの点でも `V(out)` は 2 V です。過渡解析のソルバーが
  時刻を電源に渡さないためです（`BehavioralSource::set_current_time` が呼ばれません。コードで
  読みました）。
- **波かっこの中と `.OPTIONS` の値では、倍率を受け付けません**（`.OPTIONS` では警告を出してその
  オプションを無視します）。
- **`.SUBCKT` の中で展開するのは、`R`、`C`、`L`、`V` と `I`（DC と AC の値。`PULSE` はなし）、`M`
  （4 端子、レベル 1 または BSIM4）、`X` だけです。** ほかの素子は警告を出して飛ばします。
  `PARAMS:` は受け付けません。
- **`.FUNC` は素子の値の中では呼べず、** `.PARAM` の式の中だけで呼べます（`Expression parse error`）。
- **`.IF` は `.PARAM` の値を読めません。** 条件を評価できないと、パーサーは警告を出して、すべての
  分岐を残します。
- **同じ名前の 2 つ目の `.PARAM` は無視されます。** 警告 `Failed to add ... to symbol table` が出ます。
- **`.IC` と `.NODESET` は大文字の `V(` が必要です。** `v(out)=1` は警告を出して飛ばします。
- **波形は `PULSE` だけで、DC の値の後にしか書けません。** `V1 in 0 PULSE(...)` は
  `Invalid number format: missing digits` で止まります。`SIN`、`EXP`、`PWL`、`SFFM` は何も言わずに
  捨てられ、電源は DC の値のままです。電流源の `PULSE` も同じです。`PULSE` の引数の間のカンマの後には
  空白が必要です。`PULSE` があると、DC の値と `AC` は使われず、`.DC` はその電源を掃引できません。
  `DC Sweep: Source 'V1' not found` をログに出し、プログラムは終了コード 1 で終わります（コミット
  `db0fef6` で）。
- **`.MODEL` の行と `PULSE(...)` の中では、波かっこの式に空白を入れられません。** そこでは行を
  空白でもう一度分けるので、`IS={a * 2}` は `Invalid number format` で止まります。
- **ダイオードが読むのは `IS` と `N` だけ、バイポーラトランジスタが読むのは `IS`、`BF`、`NF`、`ISE`、
  `NE`、`ISC`、`NC` とノイズのパラメーターだけです。レベル 1 の MOSFET は、式にバルク端子がなく、
  `LAMBDA` もなく、`GAMMA` はノイズの係数です。** ダイオード、バイポーラトランジスタ、レベル 1、
  BSIM4 のカードのほかのパラメーターは、警告なしに無視します。ほかのレベルの MOSFET は、知らない
  パラメーターに警告を出します（[リファレンスマニュアル](docs/REFERENCE_MANUAL.ja.md#モデル)）。
  レベル 3 は `LAMBDA`、`RS`、`RD`、`CJ`、`CJSW`、`MJSW` を警告なしに受け付けますが、使いません。
- **どのモデルにもない `LEVEL` は、何も言わずにレベル 1 になります。**
- **`S` と `Y` は、それより前に定義されていないモデル名を何も言わずに受け付け、** 既定値を使います。
- **`.PRINT`、`.SAVE`、`.STEP`、`.SENS`、`.MC`、`.TF`、`.PZ`、`.DISTO`、`.PORT`、`.WCASE`、`.OPTIMIZE` は
  読みますが、プログラムは使いません。** `.NOISE`、`.MEASURE`、`.TITLE` は無視します。
- **`.END` の後の行も読みます。**
- **`.control` ブロックの中の解析は、ngspice と同じようには実行しません。** コミット `7434904` で動かすと、
  `.control` の中の `op` では動作点が選ばれず（プログラムは既定の動作点に戻り、電流を表示しません）、
  `dc V1 0 1 0.5 V2 0 1 1` は `V1` だけを掃引しました。解析はドットのディレクティブ（`.op`、`.dc`、
  `.tran`、`.ac`）で書いてください。

## 解析

特に書いていない限り、コミット `91af284` からビルドしたプログラムで測りました。

- プログラムが使わない、ライブラリの可変刻みの経路では、方法を `TRAPEZOIDAL` に固定すると、固定刻みの
  経路が行う最初の後退オイラー法のステップなしで始まります（コードで読んだだけで、実行していません）。
- **`.TRAN` の `tmax` は効きません。** パーサーは読みますが、プログラムは `tstep` で刻みます。
  `.TRAN 10u 1m 0 2u` は、`.TRAN 10u 1m` と同じく 101 点になります。ngspice-47 は刻みを `tmax` までに
  抑え、508 点、最大の刻み 2 µs になります（コミット `d858190` で測りました）。
- **`TRTOL`、`CHGTOL`、`LTE_*` のオプション、`ERROR_EST`、`MAXORD` は効きません。** プログラムが
  使わない可変刻みの経路に働くものだからです（コードで読みました）。
- **過渡解析は、動作点から `tstart` で始まります。** SPICE は 0 から計算し、`tstart` から保存します。
  0 でステップを加えた RC ローパス（1 kΩ、1 µF）を `.tran 0.5m 2m 1m` で解くと、最初の行は t = 1 ms で
  V(out) = 0 です。厳密な値は 1 − e⁻¹ = 0.632 です（コミット `db0fef6` で）。
- **固定刻みのループは、最後にもう 1 つ、とても短いステップを踏むことがあります。**
  `run_fixed_timestepping` は `tstop` に達するまで時刻に `tstep` を足し、丸めで和が足りないと、残りの
  分のステップを踏みます。そのため `.tran 1u 200u` は 202 点になり、最後の 2 点はどちらも 200 µs です
  （コミット `db0fef6` で）。多くの回路では余分な点は 1 つ前の点と同じですが、過渡解析のスタンプが
  間違っているデバイス（下の SiC MOSFET）では違います。過渡解析の表の最後の行が 2 回出るのも、同じ
  原因です（[コマンドラインと出力](#コマンドラインと出力)）。
- **`.TEMP` があっても、DC スイープと AC 解析は `.TEMP` の値ではなく `.OPTIONS TEMP`（既定 27 °C）で
  実行します。** 動作点と過渡解析はファイルを書かず、過渡解析はログの行だけを出します。ネットリストが
  解析を求めていないためにプログラムが実行する動作点は、既定の設定を使い、`.OPTIONS` も `.TEMP` も
  使いません。
- **`.OP` と `.DC` があると、スイープだけを実行します。**
- **`.FOUR` が解析するのは最初の信号だけです。** 行のそれより後の信号は、何も言わずに無視します。

## デバイスモデル

- **BSIM4 のノイズと BSIM4 のパラメーター抽出は、別の簡略化した電流のモデルを使います。** 動作点、
  DC、過渡、AC の解析は `BSIM4Core`（`src/devices/bsim4_core.cpp`）を通して BSIM4 を評価します。
  ノイズ源（`MOSFET_BSIM4::get_noise_sources`）と `BSIM4ParameterExtractor` は、代わりに
  `src/devices/mosfet_bsim4.cpp` の `compute_drain_current` と `compute_threshold_voltage` を使います。
- **SiC MOSFET、GaN HEMT、IGBT は、AC やノイズのために電流をもう一度計算しています**（コードで
  読みました）。SiC MOSFET の `stamp_ac` と `get_noise_sources` は、RDRIFT なしで二乗則を書き直して
  おり、`stamp_ac` は g<sub>ds</sub> を独自の式から求めます。GaN HEMT は、DC、AC、ノイズのスタンプで
  RS と RD の補正の仕方が違います。IGBT のノイズ源は、バイアスにかかわらず
  g<sub>m</sub> = 2·AREA·KP·5 を使います。
- **SiC MOSFET のゲート容量は、履歴の電流の符号が逆です**（`MOSFET_SiC::stamp_transient` は右辺に
  −C/dt·V<sub>prev</sub> を足しますが、後退オイラー法のコンパニオンモデルでは +C/dt·V<sub>prev</sub>
  です）。RG を通して駆動したゲートは、駆動の全部ではなく 1/(1 + 2·RG·C/dt) に落ち着きます。
  RG = 1 kΩ、CGS = CGSO·W = 10 nF、`.tran 0.1m 1m` では、1 V のステップに対して V(g) は 0.833 V です
  （コミット `db0fef6` で）。
- **MESFET の過渡解析は、ゲートが順バイアスになると発散します。**
  `.model mz NMF cgs=1u cgd=0 cds=0` のゲートに 1 kΩ を通して 1 V のステップを加え、
  V<sub>DS</sub> = 2 V、`.tran 1u 20u` とすると、V(g) は 2 µs で −999 V、19 µs で
  9.99·10<sup>53</sup> V になり、プログラムは終了コード 0 で終わります（コミット `db0fef6` で。原因は
  未確認です）。
- **BSIM4 には、ソースとドレインの接合の DC モデルがありません。** ngspice-47 の `i(vd)` は、これらの
  GMIN の電流（10<sup>−12</sup> S と接合の電圧の積）も含むので、しきい値より下で差が見えます。
  `tests/unit/test_bsim4_reference.cpp` のカードで V<sub>GS</sub> = 0.2 V のとき、チャネルの電流は
  一致し、`i(vd)` は GMIN·V<sub>DB</sub> だけ大きくなります。
- **BSIM4 の MOBMOD 3（high-k の移動度モデル）〜6 は、警告なしに MOBMOD 0 として計算します。**
  MOBMOD 0、1、2 は BSIM4 4.8.3 のコードに従います。
- **バックゲート電圧があると、BSIM4 のしきい値が ngspice-47 と最大 10 µV 違います**（参照テストの
  カードで、V<sub>BS</sub> = 0 で 1 µV、−0.5 V から −3 V で 6〜10 µV）。原因は未確認です。BSIM4 の
  コードの k/q（8.617087·10<sup>−5</sup> V/K）は、kirimaspice が使う値（CODATA、
  8.617333·10<sup>−5</sup> V/K）と 2.8·10<sup>−5</sup> 違い、φ を約 12 µV 動かします（見積もりで、
  確かめていません）。
- **BSIM4 の gm、gds、gmb は、ドレイン電流の中心差分です**（`BSIM4Core::evaluate`）。そのため
  1 回のスタンプで電流を 7 回計算します。`BSIM4Core::compute_cdrain` は解析的な微分もまだ計算して
  いますが、使っていません。
- **レベル 3 の MOSFET は、ngspice-47 が公開されている式から離れる所で ngspice-47 と違います。**
  レベル 3 は、公開されている SPICE のレベル 3（Vladimirescu と Liu、UCB/ERL M80/7、1980）に従い、
  チャネル長変調が効かない所では ngspice-47 と 2·10<sup>−5</sup> まで一致します。KAPPA と NSUB が
  あると、ngspice-47 は VDSAT より下でもチャネル長を変え、上ではずらします。その
  dL<sup>2</sup>/(KAPPA X<sub>d</sub><sup>2</sup>) は、VDSAT より上で V<sub>DS</sub> − 7/8 V<sub>DSAT</sub>、
  下で V<sub>DSAT</sub>/8 (V<sub>DS</sub>/V<sub>DSAT</sub>)<sup>8</sup> です（結果から推定しました。文書では
  見つけていません）。公開されている式は、VDSAT より上で V<sub>DS</sub> − V<sub>DSAT</sub>、下で 0 です。
  差は VDSAT の近くで最大 4.2 %、ずっと上で 1 % 未満です。PHI のないカードは
  PHI = 2 V<sub>t</sub> ln(NSUB/n<sub>i</sub>)、n<sub>i</sub> = 1.45·10<sup>10</sup> cm<sup>−3</sup> を
  使い、ngspice-47 より 0.08 % 大きくなります（ngspice-47 の結果は n<sub>i</sub> ≈
  1.466·10<sup>10</sup> cm<sup>−3</sup> に当たります。原因は未確認です）。レベル 3 には DC の接合
  ダイオードがなく（ngspice-47 の電流は、接合が順バイアスのときそれを含みます）、温度依存もなく
  （TNOM は読みます）、過渡解析の容量もありません。BSIM1 も TNOM を読みますが、温度依存は
  ありません。VMAX と KAPPA を一緒に使うと、レベル 3 は V<sub>DS</sub> = 10 V で ngspice-47 と最大
  12 % 違います。原因は未確認です。
- **JFET はネットリストから使えません。** ライブラリには JFET のモデル
  （`include/kirimaspice/devices/jfet.hpp`）がありますが、パーサーに `J` 素子がありません。ゲートの
  容量は、現在の反復の C/dt (V − V<sub>prev</sub>) を右辺に入れています。GaN HEMT、VDMOS、
  FinFET が修正前に持っていた形です（コードで読みました。実行はしていません）。
- **FinFET は簡略化した二乗則のモデルです**（`src/devices/finfet.cpp`）。しきい値より下の電流は
  β·V<sub>t</sub><sup>2</sup>·exp(…)·(1 − exp(−V<sub>DS</sub>/V<sub>t</sub>)) で、しきい値では
  ほぼ β·V<sub>t</sub><sup>2</sup> になりますが、その上の二乗則は 0 から始まるので、そこで電流が跳びます
  （コードで読みました）。W<sub>eff</sub> は NFIN·HFIN で、フィンの反対側と上面を含みません。
  V<sub>DS</sub> < 0 のときにドレインとソースを入れ替えません。`TFIN` と `VSAT` は読みますが
  使いません（[リファレンスマニュアル](docs/REFERENCE_MANUAL.ja.md#finfetlevel12)）。
- **SiC MOSFET、GaN HEMT、VDMOS、FinFET は、過渡解析の積分法にかかわらず、容量を後退オイラー法で
  積分します。** IGBT も、台形則のもとではそうします（その `stamp_transient` は、どちらの方法でも
  (V − V<sub>prev</sub>)/dt を使います）。
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
- 標準出力の過渡解析の表と raw ファイルで、最後の時点が 2 回出ることがあります（`.TRAN 10u 5m`、
  `.TRAN 1n 30n`、`.TRAN 1u 200u` で見られ、`.TRAN 10u 1m` では見られません）。固定刻みのループが、
  丸めの残りの分のステップをもう 1 つ踏むためです（[解析](#解析)）。
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
