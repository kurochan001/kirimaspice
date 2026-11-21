# 既知の問題

[English](KNOWN_ISSUES.md) · この文書は英語版を正とします。日本語版はその訳で、両者が食い違う場合は英語版に従います。

この一覧には、測ったこと、またはコードで読んだことだけを載せています。特に書いていない限り、
測定は 2026-11-14 に、macOS 27.2（arm64）、GCC 16.2.0、CMake 4.4.4、SuiteSparse 7.6.1 で行いました。

## テスト

後の箇条書きで説明する例外をまとめます。この表が別のテスト群を追加するわけではありません。

| 対象 | 状態 | 主な障害 |
| --- | --- | --- |
| メーカーモデルとエミッタ接地のテスト（6 件） | 既知の失敗 | モデルファイルがない |
| BSIM4 の温度の参照（1 件） | 実行 | ngspice-45.2 の参照をリポジトリに置いた |
| BSIM4 の VDSAT 分岐（1 件） | 実行 | ngspice-45.2 で参照を確認した |
| BSIM4 過渡解析のインバータ列（1 件） | 無効 | 遷移がずれる。簡略化した容量モデル |
| ngspice を実行する比較 | 環境による | ngspice があるときに実行 |

- **6 件のテストが失敗します。** `tests/known_failures.txt` に並べてあります。
  - `ManufacturerModels.*`（5 件）と `BJTModelTest.SimpleCommonEmitter` は、リポジトリにない
    `tests/models/2N3904.lib` と `tests/models/1N4148.lib` を読み込みます。テストは 2N3904 に
    `BF=200`、1N4148 に `IS=2.52e-9`、`N=1.752` を期待します。追加の前に出自と再配布条件を
    確かめる必要があり、出自は未確認です。
- `CMOS_Inverter.DCTransferCharacteristic_HighInput` は、以前は出力電圧が 0 V 以上であることを
  確かめ、arm64 で -4.1e-25 V を得ていました。これは厳密解 0 V の丸め誤差なので、下限を
  -1e-12 V まで許すようにしました。
- `validation/` と `tests/validation/` の参照データの大部分は、作った ngspice の版が記録されていません。
  例外は `validation/mosfet_2stage_amplifier_ac_ngspice.csv` で、ngspice-45.2 で書き出しました。
  `validation/` を読む比較のテスト 20 件は、データの探索先を直すまでスキップされていました。
  それ以後は Linux（x86-64）の clang 20.1.8 と GCC 15.2.0 で実行し、CI run 44613253295 では
  GCC 13.3.0 と clang 18.1.3 でもすべて合格しました。ほかの環境は未確認です。
  AC 参照の CSV を書くデッキは `validation/` にあります。`validation/diode_ac_ngspice.csv` と
  `validation/resistor_mesh_*_dc.csv` を書くデッキはリポジトリにないため、これらの作られ方は
  未確認です。
- `BSIM4TemperatureValidationTest.TemperatureSweepValidation` は、
  `scripts/bench/generate_bsim4_temperature_refs.py` が ngspice-45.2 で生成した
  `tests/validation/bsim4_temperature_refs.json` を読みます。Linux（x86-64）では、実装はこの参照に
  平均 0.09%（Vth）、1.99%（Ids）、1.66%（Vdsat）で一致し、最悪の点は 0.65%、2.41%、2.44% です。
- `BSIM4ComprehensiveTest.NgspiceComparison` は `ngspice` を実行し、ngspice が入っていなければ
  スキップされます。macOS で Homebrew の ngspice-47 を入れると、実行して合格します。
- `BSIM4ComprehensiveTest.VdsatBranchSelection` は、`validation/bsim4_vdsat_branch.cir` の
  参照を読みます。ngspice-45.2 では、このデッキは id = 2.1016 A、vdsat = 0.8418 V を与え、
  テストは保存した値から 0.08% と 0.45% の差で合格します。
- `BSIM4Transient.InverterChain_MatchesNgspiceReference` は無効です（名前に `DISABLED_` があります）。
  そのコメントには、BSIM4 の過渡解析が 1 ns 付近で収束しないと書いてありますが、Linux（x86-64）では
  完走します。参照の許容 50 mV に対して最悪の誤差は n1 0.40 V、n2 0.83 V、n3 1.83 V で、いずれも
  遷移の途中です。DC レベルは一致し、シミュレーションの遷移が早く、後述の簡略化した容量モデルと
  整合します。参照のデッキは `.options method=gear` を使い、テストは台形法を使いますが、Gear2 で
  やり直しても誤差はほとんど変わりません。
- コミット `02261a2` の macOS では、CTest のテストは 1188 件でした。1158 件が合格、22 件がスキップ
  （当時はデータを別のディレクトリで探していた比較のテスト 20 件、`TemperatureSweepValidation`、
  `VdsatBranchSelection`）、7 件が失敗、1 件が無効でした。
- **時間刻みと LTE のテストの約 34 件は、動かない回路を確かめています。** `test_predictor_corrector_lte`、
  `test_trapezoidal_variable_step`、`test_multistage_rc`、`test_lte_tolerance`、
  `test_initial_timestep_control` のテストは、`UIC` なしで DC 電源の動作点から始まるので、回路は静止した
  ままで、LTE のコードが何をしても確認は成り立ちます（`63ce9a6` でテストを読みました）。
- **失敗しえないテストや、確かめることの少ないテストがあります**（`63ce9a6` で読みました）。VDMOS の
  `LinearRegion` と `SaturationRegion` は電源が固定するノードを読み、MESFET のテストはどれもドレイン電流の
  値を確かめず、伝送線路のテストの「ngspice validation」の参照は DC の値です。`bsim4_dc_smoke` は Id が有限で
  あることだけを確かめ、`BSIM4NgspiceCompareTest.IdsVsNgspice_T27C` は誤差 100 % まで合格し（コメントは
  30 % と書いています）、DVT0、DVT1、ETA0、K3 を BSIM4 の既定値でない 0 にしています。BJT の検証の
  プログラムのいくつかは名前と違う領域を確かめるか、5〜21 % を許します。レベル 3 の「Meyer」容量の
  テストは、どのスタンプも使わない補助関数から、線形領域で Cgs = Cgd = 2/3 Cox W L を期待します。
- **ソースツリーや、一時ディレクトリの決まった名前に書き込むテストがあります。**
  `ParameterSweepPerf.NoStepFallsBackSingleRun`（ソースツリーの `tmp_param_perf.cir`）、`NetlistParser` の
  include のテスト（`$TMPDIR/kirimaspice_test/`）、`test_rc_ac_validation`
  （`kirimaspice_ac_reference_test.csv`）で、同時に動かした 2 つのテストの実行がこれらのファイルを共有します
  （`63ce9a6` で読みました）。

## ネットリストの入力

[ユーザーマニュアル](docs/USER_MANUAL.ja.md)と[リファレンスマニュアル](docs/REFERENCE_MANUAL.ja.md)を
書くときに、コミット `91af284` からビルドしたプログラムを動かして見つけたものです。この節で SPICE と
ngspice について書いていることは、それぞれのマニュアルによります。ngspice との 5 つの違い（倍率、AC 解析の
入力、電源の電流、入れ子の `.DC`、`gnd`）は、ngspice-47 を動かして確かめ、
[NETLIST_CONVENTIONS.md](docs/spec/NETLIST_CONVENTIONS.ja.md) のとおり ngspice に合わせてなくしました。
その文書には、そこで決めていない違いも挙げてあります。

- **`B` 電源には `I(<電源>)` がなく、`.PARAM` の名前も読めません。** どちらかを含む式はエラーです
  （`Undefined symbol`）。
- **波かっこがあってもなくても式の中と、`.OPTIONS` の値では、倍率を受け付けません**（`.OPTIONS` では
  警告を出してそのオプションを無視します）。波かっこのない `.PARAM` の値は、数でなければ式として読むので、
  `.param r = 2k` は 2000 ですが、`.param r = 2*1k` はエラーです。ngspice-47 では 2000 です（2026-11-21 に
  `21bb991` で実行）。
- **`.SUBCKT` と `X` の行では `PARAMS:` を受け付けません。** パラメーターは、ピンの後に `名前=値` で書きます。
- **`E`、`F`、`G`、`H` には `POLY` の形がありません。** 出力のノードの後に `POLY(...)` がある行はエラーです。
- **`.FUNC` は素子の値の中では呼べず、** `.PARAM` の式の中だけで呼べます（`Expression parse error`）。
- **`.IF` は `.PARAM` の値を読めません。** 条件を評価できないと、パーサーは警告を出して、すべての
  分岐を残します。
- **同じ名前の 2 つ目の `.PARAM` は無視されます。** 警告 `Failed to add ... to symbol table` が出ます。
- **サブ回路の本体で、閉じる波かっこのない `.param x={1k` は、何も言わずに何も設定しません。** そのとき
  `x` を使う素子がエラーになります（2026-11-21 に `21bb991` で実行）。
- **BSIM1（`LEVEL` 9）の `W` と `L` の既定値は 10 µm と 1 µm です。** ngspice-47 がどの既定値を使うかは
  未確認です。その BSIM1（`LEVEL` 4）は、パラメーターのないカードで `nan` を出します（2026-11-21 に実行）。
  レベル 3、8/49、14/54 は ngspice-47 の既定値を使います。
- **`.IC` と `.NODESET` は大文字の `V(` が必要です。** `v(out)=1` は警告を出して飛ばします。
- **SPICE が `.TRAN` から既定値を決める波形の引数は、書かなければなりません。** `PULSE` は 7 つすべて、
  `SIN` は少なくとも `VO VA FREQ`、`EXP` は 6 つすべて、`SFFM` は少なくとも `VO VA FC` を取ります
  （コードで読みました）。DC の値のない `SFFM` の電源は、動作点で `VO` になります。ngspice-47 では
  0 V です（コミット `eaf0629` で測りました）。
- **ダイオードが読むのは `IS`、`N`、`CJO`、`VJ`、`M`、`RS`、`TT`、`BV`、`IBV` だけで、`FC`、`KF`、`AF`
  などは読まず、警告で名指しします（FC は 0.5 です）。バイポーラトランジスタは `RE`、`RC`、`CJS`、`XTF`、`VTF`、`ITF`、`PTF`、`XCJC`、`FC` を
  読みません。レベル 1 の MOSFET には、キャパシタンスとバルクの接合がありません。**
  これらのデバイスと MOSFET の各レベルのカードのほかのパラメーターは、警告で名指しします（[リファレンスマニュアル](docs/REFERENCE_MANUAL.ja.md#モデル)）。
  その中には、ダイオードとレベル 1 の MOSFET の `TNOM`（27 °C を使います）と、ダイオードの `EG` と `XTI`
  （1.11 eV と 3 を使います）があります（2026-11-21 に `21bb991` で実行）。
  レベル 3 は `LAMBDA`、`RS`、`RD`、`CJ`、`CJSW`、`MJSW` を警告なしに受け付けますが、使いません。
- **バイポーラトランジスタは、NPN と PNP に共通のデバイスで Gummel–Poon の電流を扱いますが、寄生成分に
  制限があります。** 電流とその解析的な微分は `BJT::evaluate` で一緒に求めます。順方向能動、飽和、
  逆方向能動、高注入での電流は、ngspice-47 と相対誤差 2e-5 以内で一致します（2026-11-15 に実行。
  `tests/unit/test_bjt_gummel_poon.cpp`）。RB、RBM、IRB からベース抵抗を求めるアクセサーはありますが、
  その抵抗はスタンプに入りません。RE、RC、基板接合、XTF、VTF、ITF、PTF、XCJC はモデル化せず、FC は
  0.5 に固定しています。過渡解析は接合の電荷ではなく電圧依存の容量を積分し、AC と過渡解析は B-E の電荷の
  VBC による微分を含みません。温度は IS、ISE、ISC、BF、BR を SPICE の Gummel–Poon の法則で変えます
  （`ba291cd` から）が、接合電位と空乏層容量は変えません（`ba291cd` でコードを読みました）。
- **`S` と `Y` は、それより前に定義されていないモデル名を何も言わずに受け付け、** 既定値を使います。
- **`.SAVE`、`.STEP`、`.SENS`、`.MC`、`.TF`、`.PZ`、`.DISTO`、`.PORT`、`.WCASE`、`.OPTIMIZE`、`.PRINT DC`、
  `.PRINT AC` は読みますが実行せず、それぞれ警告で名指しします。`.PRINT TRAN` は表示します。**
  `.NOISE`、`.MEASURE`、`.TITLE` と知らないディレクティブは、警告を出して無視します。
- **`.END` の後の行も読みます。**
- **タイトルの前の空の行は飛ばし、空でない最初の行をタイトルにします。** ngspice-47 は空でも 1 行目を
  タイトルにするので、空の行で始まるネットリストは、ここでは最初の素子を失います。2026-11-15 に同じ
  ネットリストを両方で動かして確かめました。2 行目が `V1 a 0 1` のとき、ngspice-47 は
  `i(v1) = -1.00000e-03` を出し、このプログラムには `V1` がありませんでした。テストの多くは、空の行で
  始まるネットリストを書いています。
- **`.control` ブロックの中の解析は、ngspice と同じようには実行しません。** コミット `7434904` で動かすと、
  `.control` の中の `op` では動作点が選ばれず（プログラムは、解析の行がないネットリストと同じ動作点を実行します）、
  `dc V1 0 1 0.5 V2 0 1 1` は `V1` だけを掃引しました。`2826a81` では、`.control` の中の `tran` も実行
  しません。`dc` と `ac` は実行し、`21bb991` でも `dc` は何も言わずに最初の電源だけを掃引します。
  解析はドットのディレクティブ（`.op`、`.dc`、
  `.tran`、`.ac`）で書いてください。

## 解析

特に書いていない限り、コミット `91af284` からビルドしたプログラムで測りました。

- プログラムが使わない、ライブラリの可変刻みの経路では、方法を `TRAPEZOIDAL` に固定すると、固定刻みの
  経路が行う最初の後退オイラー法のステップなしで始まります（コードで読んだだけで、実行していません）。
- **`TRTOL`、`CHGTOL`、`LTE_*` のオプション、`ERROR_EST`、`MAXORD` は効きません。** プログラムが
  使わない可変刻みの経路に働くものだからです（コードで読みました）。
- **`.OP` と `.DC` があると、スイープだけを実行します。**
- **ライブラリの可変刻みの経路は、静止した回路で、次数を固定した BDF2 や BDF3 だと失敗します。**
  1 kΩ、1 µF の RC 回路を動作点から（`UIC` なしで）始めると、t = 0 で刻みが 10<sup>-12</sup> s まで
  縮んで失敗します。BDF1 と次数の制御は終わります。`BDFOrderComparisonTest.FinalValueConvergence` は、
  この 2 つの実行を飛ばして合格します（2026-11-20 に測りました）。
- **収束の補助は、既定ではどれも働きません。** ソースステッピング、Gmin ステッピング、動的 Gmin、
  継続法、疑似過渡への切り替えは、`.OPTIONS` で求めたときだけ働きます。そのため、Newton 法だけで
  終わらない DC の求解は失敗します。ngspice-47 の既定の動きは確かめていません。
- **過渡解析は ngspice-47 より時間がかかります。** 100 Ω と 10 pF を 200 段つないだはしご（未知数 201）を
  `.TRAN 1n 10u` で 10 001 点計算すると 2.1 秒でした。`tmax` を 1 ns にした ngspice-47 は 10 024 点で
  0.36〜0.40 秒でした（macOS arm64、2026-11-21）。原因は調べていません。
- **デバイスのうち、接合に GMIN を持つのはダイオード（`36d8f96` から）とバイポーラトランジスタだけです。**
  レベル 1 とレベル 3 の MOSFET、BSIM3、SiC MOSFET、VDMOS、JFET は、GMIN を `stamp_dc_linear` に
  書き込みますが、非線形のデバイスではどのソルバーもこれを呼ばないので、GMIN がありません。SPICE は
  GMIN をバルクの接合に置きますが、これらのモデルの多くには接合がありません（`63ce9a6` でコードを
  読みました）。
- **`.AC LIN 2` は両端を取りますが、ngspice-47 は始まりだけです。** `.ac lin 2 1 10` は、ここでは 1 と
  10 Hz、ngspice-47 では 1 Hz です。ngspice-47 は 3 点より少ないと直線の刻みを 0 にします（2026-11-21 に
  実行）。`<n>` が求める 2 点を保ちます。`DEC` と `OCT` は ngspice-47 の格子を使います。
- **ライブラリの入力換算ノイズ（`NoiseSolver`）は、入力の電源からの電圧利得ではなく、入力のノードからの
  トランスインピーダンスで割ります**（`63ce9a6` でコードを読みました。テストはありません）。
- **最適化の `LD_SLSQP` には勾配が渡りません。** `nlopt_objective_wrapper` は `grad` を無視します
  （`63ce9a6` でコードを読みました。実行しては確かめていません）。

## デバイスモデル

- **BSIM4 のノイズと BSIM4 のパラメーター抽出は、別の簡略化した電流のモデルを使います。** 動作点、
  DC、過渡、AC の解析は `BSIM4Core`（`src/devices/bsim4_core.cpp`）を通して BSIM4 を評価します。
  ノイズ源（`MOSFET_BSIM4::get_noise_sources`）と `BSIM4ParameterExtractor` は、代わりに
  `src/devices/mosfet_bsim4.cpp` の `compute_drain_current` と `compute_threshold_voltage` を使います。
- **BSIM4 には、ソースとドレインの接合の DC モデルがありません。** ngspice-47 の `i(vd)` は、これらの
  GMIN の電流（10<sup>−12</sup> S と接合の電圧の積）も含むので、しきい値より下で差が見えます。
  `tests/unit/test_bsim4_reference.cpp` のカードで V<sub>GS</sub> = 0.2 V のとき、チャネルの電流は
  一致し、`i(vd)` は GMIN·V<sub>DB</sub> だけ大きくなります。
- **BSIM1（`LEVEL` 9）は簡略化したモデルで、P のデバイスが N のデバイスの鏡像になっていません。**
  VFB = −0.8 V（N）または +0.8 V（P）、PHI = 0.7、K1 = 0.5、TOX = 0.02 µm、MUZ = 600、
  W/L = 10 µm / 1 µm、|VD| = |VG| = 1 V で、N のデバイスは 1.033e-4 A、ngspice-47 の BSIM1（その
  LEVEL 4）は 2.087e-4 A、P のデバイスは 0 A です（2026-11-15 に実行）。P のデバイスのスタンプは電流を
  線形化しておらず、しきい値は |VFB| と PHI + V<sub>SB</sub> を使い、接合の容量は N のデバイスの負の
  V<sub>BD</sub> を順バイアスとして扱います（コードで読みました）。
- **BSIM3（`LEVEL` 8 と 49）は簡略化したモデルで、BSIM3v3 ではありません**（`src/devices/bsim3_core.cpp`、
  約 450 行）。TOX = 3 nm、VTH0 = 0.5 V、U0 = 670 cm²/Vs、VSAT = 8e4 m/s、W/L = 10 µm / 1 µm、
  VD = VG = 1 V で 6.230e-4 A になりますが、ngspice-47 の BSIM3 は 3.151e-4 A です（2026-11-15 に実行）。
  U0 は大きさにかかわらず cm²/Vs として読みます。ngspice-47 は 1 以下の U0 を m²/Vs として読みます
  （`u0=0.067` と `u0=670` で同じ電流）。V<sub>DS</sub> < 0 では電流が流れません（逆方向のモードも
  接合のダイオードもありません。VD = −1 V、VG = 1 V で 0 A）。現実的な移動度では、動作点が収束しない
  ことがあります。VDD = 1.8 V を 10 kΩ を通して、VG = 1 V、L = 0.18 µm、W = 1 µm、U0 = 670 では
  100 回の反復で止まります。ngspice-47 では V(d) = 0.6291 V です。しきい値も BSIM3 のものではありません。
  上のカードに K1 = 0.5、K2 = 0 を加え、VD = 1 V とすると、項 −DVT0·atan(DVT2·V<sub>BS</sub> /
  √(DVT1·L<sub>eff</sub>)) が |V<sub>BS</sub>| = 0.3 V でしきい値を約 ±3.3 V 動かし、
  √(|V<sub>BS</sub>| + φ) のために順方向のバックゲート電圧がしきい値を上げ、VG がしきい値を越えて上がると
  電流が 6 桁落ちます。VG = 0.4999 V で 1.90e-4 A、0.5001 V で 4.1e-11 A です（2026-11-21 に `21bb991`
  で実行。バックゲート電圧の項はコードで読みました）。
- **BSIM4 の容量は、BSIM4 の接合を持つ簡略化した Meyer のモデルで、** BSIM4 の電荷のモデルではありません
  （`BSIM4Core::compute_capacitances`）。接合の周囲長は `PS` と `PD` だけから取り、素子の行に書かなければ 0 です。
  ngspice-47 はそのとき周囲長を取ります。カードに接合のパラメーターがなく AS = 1e-10 m² のとき、
  ソースの容量は逆バイアス 1 V で 4.331e-14 F で、底面の接合だけでは 3.536e-14 F です。残りは、既定の側面の
  容量に周囲長 2 W を掛けたものに合います（2026-11-15 に実行。ngspice の決まりは読んでいません）。
  外側のフリンジ容量 CF はモデルにありません。線形領域では Cgs = Cgd = 2/3 Cox W L（合わせて
  4/3 Cox W L）です。容量の飽和速度は VSAT − AT·(T − TNOM)（ケルビン）ですが、電流は AT·(T/TNOM − 1) を
  使うので、既定の AT = 3.3e4 では TNOM + 2.4 K で 0 になります。入力容量は 27 °C と 28 °C で 7.67e-14 F、
  30 °C で 1.535e-13 F です。`MULT_Q` は使いません。カードのものは警告で名指ししますが、素子の行のものは
  名指ししません（2026-11-21 に `63ce9a6` で実行）。
- **BSIM4 の MOBMOD 3（high-k の移動度モデル）〜6 は、警告なしに MOBMOD 0 として計算します。**
  MOBMOD 0、1、2 は BSIM4 4.8.3 のコードに従います。
- **バックゲート電圧があると、BSIM4 のしきい値が ngspice-47 と最大 10 µV 違います**（参照テストの
  カードで、V<sub>BS</sub> = 0 で 1 µV、−0.5 V から −3 V で 6〜10 µV）。原因は未確認です。BSIM4 の
  コードの k/q（8.617087·10<sup>−5</sup> V/K）は、kirimaspice が使う値（CODATA、
  8.617333·10<sup>−5</sup> V/K）と 2.8·10<sup>−5</sup> 違い、φ を約 12 µV 動かします（見積もりで、
  確かめていません）。V<sub>bseff</sub> の下限 VBSC が `74ba4d2` の前は 0 だったことが原因の 1 つで、
  これを求めると、V<sub>BS</sub> = −1 V での電流は ngspice-47 に対して相対 −5.0e-5 から +1.1e-5 に
  なりました。上のしきい値の差がどれだけ残るかは未確認です（参照テストのカードで V<sub>th</sub> を測り
  直せば分かります）。
- **`VTH0` のない BSIM4 のカードは 0.7 V（P は −0.7 V）を使います。** ngspice-47 は、BSIM4 のマニュアルの
  パラメーター一覧の注にあるように、ほかのパラメーターから求めます（`VFB`、Φs、`K1` から。そのとき `K1` と
  `K2` をどう求めるかは未確認）。パラメーターのないカードで W = L = 5 µm、VD = VG = 2 V のとき、ここでは
  2.626e-4 A、ngspice-47 では 5.650e-4 A です。カードに `VTH0=0.7` を書くと、どちらも 2.6256e-4 A です。
  既定値から VFB + Φs + K1√Φs で求めた 0.274 V を与えると、ngspice-47 は 4.928e-4 A です（2026-11-21 に
  実行）。
- **BSIM4 のパラメーターと制限のいくつかは、BSIM4 のものではありません。** `RDSWMIN`、`RDSMOD`、
  `TEMPMOD` はカードから読みません。`DROUT = 0` のカードは −8.7e5 A、`DSUB = 0` のカードは 1.2e-3 A を
  与えますが、ngspice-47 ではデバイスが切れます。ほかの指数の制限（AGS、V<sub>gsteff</sub> の平滑化）は、
  BSIM4 の 5.83e14 ではなく `MAX_EXP` = 2.353853e15 を使います（ngspice-47 とは比べていません）。
  `DVT1 = 0` のための保護と、DIBL によるずれを ±0.5 V に抑えることは、BSIM4 にはありません（2026-11-21 に
  `63ce9a6` で実行し、コードで読みました）。
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
- **バックゲートが順バイアスのとき、ngspice-47 のレベル 1 の g<sub>mbs</sub> は自身の電流の微分では
  ありません。ここでの g<sub>mb</sub> は微分です。** `vto=0.7 kp=1e-4 gamma=0.5 phi=0.6`、W/L = 10 µm / 1 µm、
  VGS = 1.5 V、VDS = 2 V、VBS = 0.2 V で、どちらも 373.723 µA で、VBS に対して 2.79e-4 A/V で増えますが、
  ngspice-47 は g<sub>mbs</sub> = 3.348e-4 S を出します。これは g<sub>m</sub>·GAMMA/(2·sarg) で、sarg は
  順バイアスで平方根の代わりになる式です（2026-11-21 に `21bb991` で実行）。
- **ノイズ源が SPICE と違います**（ライブラリだけです。プログラムは `.NOISE` を実行しません。`63ce9a6` で
  コードを読みました）。レベル 3 のチャネルの熱雑音は 4kT·(8/3)·g<sub>m</sub> で、SPICE の
  4kT·(2/3)·g<sub>m</sub> の 4 倍で、逆方向のモードでは 0 です。BSIM3、BSIM4、GaN HEMT、BSIM1 の P の
  デバイスも 8/3 を使い、レベル 1、MESFET、FinFET、IGBT、BSIM1 の N のデバイスは 2/3 です。レベル 1 の
  フリッカーノイズには C<sub>ox</sub> がなく、`EF` を面積に掛けています。ngspice-47 では
  KF·I<sub>d</sub><sup>AF</sup>/(f<sup>EF</sup>·C<sub>ox</sub>·W·L) です。抵抗、レベル 1、レベル 3 は
  ノイズの温度に 300.15 K を、MESFET と SCR は TNOM を使います。ダイオードは 1 fA より下でショット雑音を
  出しません。
- **MESFET のレベルは ngspice のものと違います。** ここでは `LEVEL=1` が Curtice のモデル（tanh）、`LEVEL=2` が
  Statz のモデルです。ngspice-47 の `LEVEL=1` は Statz のモデルで（このプログラムの `LEVEL=2` と同じ電流です。
  BETA 0.1、VTO −2 V、B 0.3、ALPHA 2、VGS = 0、VDS = 1 V で 0.240741 A）、その `LEVEL=2` は別のモデルです。
  そのため、`LEVEL` のない ngspice のカードは、ここでは Curtice の電流になります（2026-11-21 に実行）。
  ngspice の MESFET のゲートの接合と電荷のモデルはなく、`CGS`、`CGD`、`CDS` は一定です。
- **GaN HEMT は `VBD` より上で g<sub>ds</sub> に 1 mS を加えますが、それに合う電流はありません**
  （`63ce9a6` でコードを読みました）。**VDMOS は、`VBD` より上では Newton 法の反復ごとに「entered breakdown region」と警告します。**
  1 MΩ を通して 700 V を加え、`VBD = 600` とすると、この警告を 97 回出して 600.01 V に落ち着きます
  （2026-11-21 に `abb6804` で実行）。
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
- **SiC MOSFET、GaN HEMT、VDMOS、FinFET、MESFET は、過渡解析の積分法にかかわらず、容量を後退
  オイラー法で積分します**（コードで読みました）。
- **有損失線路 `O` の過渡解析は近似です。** DC と AC は正確な 2 ポートを使います。過渡解析は、定数
  パラメーターの（Bergeron の）線路を最大 16 区間つなぎます。各区間は無損失で、損失を両端にまとめて置きます
  （`TLineLossy`）。そのため、16 区間あたりの損失が大きい線路（RC 線路など）には従いません。1 V のステップを
  50 Ω を通して L = 250 nH/m、C = 100 pF/m、TD = 10 ns の線路に入れ、50 Ω で終端すると、遠端は ngspice-47 の
  LTRA と 2·10<sup>−5</sup> V（R = 0.5 Ω/m）、1·10<sup>−4</sup> V（R = 5 Ω/m）以内、近端は 1.3·10<sup>−3</sup> V
  以内で一致します。波の縁では、区間の遅れを線形補間で読むので、縁が刻みの約 √M 倍（M は区間の数）に
  広がります。刻み 0.1 ns、R = 5 Ω/m では、縁が届いて 0.2 ns 後の遠端が 0.37 V で、LTRA は 0.45 V です。刻み
  0.02 ns では一致します（2026-11-21 に実行）。刻みは各区間の遅れ TD/M 以下になります。
- **互いに結合した 3 つ以上のインダクタは、2 つずつ結合します。** それぞれの `K` は、自分の 2 × 2 の
  インダクタンス行列の逆行列が 2 つのインダクタに加える分を加えます（`MutualInductance::inverse_inductance`）。
  AC でも過渡解析でも同じです。1 組なら正確ですが、`K` の行で互いに結合した 3 つのインダクタでは
  正確ではありません。1 H の 3 つを、どの 2 つの間も k = 0.5 で結合すると、3 × 3 の行列の逆行列の
  対角は 3/2 で、2 つずつの和は 5/3 です（分数で正確に計算しました）。
- 動く線形ソルバーは KLU だけです。ライブラリの SuperLU_DIST のバックエンドは仮置きで、プログラムには
  それを選ぶオプションがありません。

## コマンドラインと出力

- DC スイープでは SSF のファイルを書きません。
- SSF の出力形式の仕様は、このリポジトリにはまだ公開していません。
- **SSF は電圧と電流を 1 µV と 1 µA の整数で保存し、倍率をファイルに書きません。** そのため 50 pA は 0 と
  して保存されます。ブロックの時刻は時刻の 10⁹ 倍の切り捨てで（`.tran 0.3n 3n` で 0、0、0、0、1、1、…）、
  ヘッダは版 3.4 と書き、`file.version` は 4.3 と書きます（2026-11-21 に `63ce9a6` で実行し、コードで
  読みました）。
- **`tools/kirimaspice_pyapi.py` の `run_netlist` は、ネットリストの文字列を一時ディレクトリに書いて
  そこで実行します。** そのため、相対パスの `.include` はこれを通すと失敗し、kirimaspice は 1 で終わります
  （2026-11-21 に `63ce9a6` で実行）。

## プラットフォーム

- macOS（arm64）では、GCC 16.2.0、Homebrew の clang 23.1.4（libc++、OpenMP あり）、Apple clang 21.0.0
  （OpenMP なし）のテストの結果が同じです。
- Ubuntu 24.04（x86-64）では、CI が GCC 13.3.0 と clang 18.1.3（libstdc++）、CMake 3.31.6、
  `libsuitesparse-dev` の SuiteSparse 7.6.1 でビルドとテストを行います。結果が今の macOS と同じかは
  未確認です。CI のテストの要約で分かります。
- 2026-11-14 に、arm64 の新しい `ubuntu:24.04` のコンテナ（macOS 上の Colima）で、README の手順どおりに
  行いました。GCC 13.3.0、CMake 3.28.3、Ninja 1.11.1、Python 3.12.3、Git 2.43.0、`libsuitesparse-dev`
  1:7.6.1+dfsg-1build1 で、ビルドでき、登録された 1097 件のテストのうち、1066 件が合格、23 件がスキップ、
  1 件が無効で、一覧の 7 件が macOS（arm64）と同じく失敗しました。
- README が最低の版としている CMake 3.25 は試していません。試した中でいちばん古いのは 3.28.3 です。
- Windows は試していません。

## ソースコード

2026-11-21 に、コミット `9aa304c` で数えました。警告はコミット `81bfb2a` で数えました。

- **コンパイラの警告。** macOS の GCC 16.2.0 で全体をビルドすると、警告の行は 559 行です。いくつもの
  ファイルのヘッダで繰り返されるものを 1 つと数えると 153 か所で、そのうち 150 か所が kirimaspice 自身の
  ソースにあります。内訳は `-Wunused-parameter` が 125、`-Wunused-variable` が 17、
  `-Wunused-but-set-variable` が 4、`-Woverloaded-virtual` が 3、`-Wunused-function` が 1 です。ほかの
  コンパイラでは数えていません。
- **大きいファイル。** 2000 行を超えるソースのファイルが 5 つあります。`src/parser/netlist_parser.cpp`
  （6509 行）、`tests/unit/test_parser.cpp`（4657 行）、`src/devices/bsim4_core.cpp`（3082 行）、
  `src/circuit/circuit_graph.cpp`（2135 行）、`tests/unit/test_bsim3.cpp`（2009 行）です。
- **日本語のコメント。** `src/` と `include/` の 11 のファイルの 46 行に日本語があります。多いのは
  `mosfet_bsim4_params.hpp`（19 行）と `mosfet_bsim3_params.hpp`（13 行）です。テストを含めると 17 の
  ファイルの 56 行です。
- **補助のディレクトリが 2 つあります。** `include/kirimaspice/util/` と `include/kirimaspice/utils/` です。
- **キャパシタ、インダクタ、相互インダクタンスは、コンパニオンモデルの式を自分で書いています。** ダイオード、
  BJT、BSIM3、BSIM4、IGBT が使う `device_utils::capacitor_companion` を使いません。数値は同じですが、式が
  1 か所に書かれていません（`63ce9a6` でコードを読みました）。
- **`MonteCarloBatchExecutor::execute_dc_monte_carlo_gpu` に GPU のコードはありません。**
  `src/analysis/monte_carlo_gpu.cpp` は「Using fast CPU path (CUDA off)」と記録し、CPU の経路を実行します。
