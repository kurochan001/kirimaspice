# 設計

[English](ARCHITECTURE.md) · [プロジェクトの README](../README.ja.md) · この文書は英語版を正とします。日本語版はその訳で、両者が食い違う場合は英語版に従います。

**2026-11-21 更新**、コミット `2826a81` に合わせています。この文書は、kirimaspice の作りを説明します。
ライブラリの部品、ネットリストがそれらを通る道筋、設計がまだ意図どおりになっていない箇所です。コードを
読んで書きました。実行しても確かめたものは、そう書いています。場所は行番号ではなく、ファイルと関数で
示します。コードに手を入れるときは [DEVELOPMENT](DEVELOPMENT.ja.md) を、用語は[用語集](GLOSSARY.ja.md)を
見てください。

## 目次

- [全体](#全体)
- [ディレクトリ](#ディレクトリ)
- [プログラムの 1 回の実行](#プログラムの-1-回の実行)
- [パーサー](#パーサー)
- [回路](#回路)
- [デバイス](#デバイス)
- [方程式と線形ソルバー](#方程式と線形ソルバー)
- [DC 解析](#dc-解析)
- [過渡解析](#過渡解析)
- [AC 解析](#ac-解析)
- [ライブラリにあるほかの解析](#ライブラリにあるほかの解析)
- [設計がまだ意図どおりでない箇所](#設計がまだ意図どおりでない箇所)

## 全体

kirimaspice は、静的ライブラリ `kirimaspice_lib` と、コマンドラインを読んでライブラリを呼ぶプログラム
`kirimaspice`（`src/main.cpp`）からなります。ライブラリはネットリストを回路と設定に変換し、デバイスから
修正節点解析（MNA）の方程式を組み立て、KLU で解き、結果を書き出します。

```mermaid
flowchart LR
    N[ネットリスト] --> P[NetlistParser<br/>parse_file]
    P --> C[CircuitGraph<br/>デバイス、ノード、枝]
    P --> S[SimulationConfig<br/>解析、オプション、モデル]
    C --> X[expand_all_subcircuits]
    X & S --> M{"main.cpp<br/>がパーサーの読んだ<br/>解析を実行する"}
    M --> DC[DCSolver]
    M --> TR[TransientSolver]
    M --> AC[ACSolver]
    DC & TR & AC --> K[ISparseLinearSolver<br/>を通した KLU]
    DC & TR & AC -->|結果| W["main.cpp が src/output の<br/>書き出しを呼ぶ"]
```

## ディレクトリ

ヘッダーは `include/kirimaspice/<ディレクトリ>/`、ソースは `src/<ディレクトリ>/` にあり、名前空間は
おおむね `kirimaspice::<ディレクトリ>` です。例外は次のとおりです。`core/types.hpp` と `core/matrix_types.hpp`
は `kirimaspice` にあります。`FourierAnalyzer`、`DistortionSolver`、`PoleZeroSolver`、`TransferFunctionSolver`
は、`kirimaspice::solvers` ではなく `kirimaspice` にあります。`core/device_utils.hpp` の後半は
`kirimaspice::device_utils` にあります。

| ディレクトリ | 中身 |
| --- | --- |
| `parser` | `NetlistParser`。前処理、行、ディレクティブ、素子の行、式 |
| `circuit` | `CircuitGraph`。ノード、デバイス、枝。`SubcircuitDefinition` と展開 |
| `devices` | 基底クラス `Device` とデバイスモデル |
| `solvers` | DC、過渡、AC とほかの解析。`ConvergenceChecker`。線形ソルバーのインターフェースと KLU |
| `analysis` | モンテカルロ、ワーストケース、NLopt による最適化、BSIM4 のパラメータ抽出 |
| `measure` | `.MEASURE` 風の測定をする `MeasureEngine` |
| `output` | SPICE3 raw（ASCII とバイナリ）、SSF、AC の CSV、Touchstone、WAV |
| `core` | 型（`SimulationConfig`、`IntegrationMethod`）、行列（`SparseMatrix`、`MNASystem`）、定数、デバイスの補助 |
| `util`、`utils` | ヘッダーだけ。`util` に文字列の補助、倍率（`units.hpp`）、電源の書式。`utils` に `MemoryPool` |

## プログラムの 1 回の実行

`src/main.cpp` は次のことをします。

1. メッセージを、標準出力を書き出した後に `kirimaspice: <レベル>: <本文>` の形で標準エラーに書く
   ロガー（`StderrAfterStdout`）を設定し、オプション（`-o`、`--raw-out`、`--raw-binary-out`、
   `--wav-*`、`--log-level`、`--version`、`--help`）を読みます。知らないオプション、値のないオプション、
   2 つ目のネットリストは、終了コード 2 のエラーです。終了コードは kirimasim と同じで、0 は終了、1 は解析の
   失敗、2 はコマンドラインか入力の誤り（書けない出力ファイル、`output::OutputFileError` を含む）、3 は
   内部の誤りです。
2. `NetlistParser::parse_file` を呼びます。これは回路と `SimulationConfig` を返します。`.TEMP` があると、
   最初の温度が基準の温度になります。
3. `CircuitGraph::expand_all_subcircuits` を呼びます。展開はパーサーではなく、プログラムの仕事です。
4. 解析を、パーサーが `SimulationConfig` に記録したものから取ります。`op_requested`（`.OP`）、
   `dc_sweep_enabled`（完全な `.DC`）、`tran_requested`（完全な `.TRAN`）、`ac_requested`（`.AC`、または
   `.control` のブロックの `ac` のコマンド）です。そのため解析の行は、継続行、`.INCLUDE`、`.LIB`、`.IF` の後で、
   ほかのディレクティブと同じに数えます。どれもないネットリストは、`.OP` と同じく動作点を実行します。
   `.OP` が求める動作点は、DC スイープに置き換わります。
5. `run_analyses` を通して解析を実行し、結果を書き出します。`.TEMP` の値ごとに、その温度を入れた
   `SimulationConfig` で 1 回ずつ実行します（`.TEMP` がなければ 1 回です）。

| ネットリストにあるもの | 実行するもの | 標準出力 | ファイル |
| --- | --- | --- | --- |
| 完全な `.DC` | DC スイープ | 要約 | raw（ASCII、バイナリ） |
| `.OP` でスイープなし、または解析の行がない | 動作点 | `V(...)` と `I(...)` | SSF、raw（ASCII、バイナリ） |
| 完全な `.TRAN` | 過渡解析。その後に `.FOUR` を、その行の信号ごとに | `.FOUR` の結果、続いて `.PRINT TRAN` の表 | SSF、raw（ASCII、バイナリ）、WAV |
| `.AC` | AC | なし | 必ず `<base>_ac.csv` |

複数の解析が raw ファイルを書くと、それぞれがプロットを後ろに足します。SSF ファイルは、それぞれが
書き直します。`.TEMP` に複数の値を並べると、raw ファイルにはすべての実行のプロットが入り、各実行の
SSF、WAV、AC の CSV の名前には、その温度が付きます（`out_27C.ssf`、`out_ac_27C.csv`）。`.FOUR` の行は、
過渡解析のソルバーの中で評価します。

## パーサー

`NetlistParser::parse_file`（`src/parser/netlist_parser.cpp`）は、段階を追って処理します。

1. テキストの前処理。ファイルを読むときに CR LF の行の終わりの CR を除き、次に `.INCLUDE`（再帰的で、循環を検出します。`.INC` はその短い形です）、`.LIB` の節
   （10 段まで入れ子にできます）、継続行です。空の行は捨て（空白だけの行は残します）、継続行はつなぎ、
   インクルードしたファイルは差し込み、段階 2 の `.IF` と `.CONTROL` の行は取り除くので、`ParseError` の
   行番号は前処理の後の行を数えます。
2. 呼び出し側が上書きした値を優先して、グローバルの `.PARAM` を解決します。
   その値で条件分岐を選び、次に `.CONTROL` ブロックを取り出します。
3. PEGTL による処理。文法はテキストをタイトルの行とほかの行に分けるだけで、それぞれの行は
   `process_line` に渡ります。その中の解析は、手書きの字句の切り出し（`util::split_netlist_tokens`）です。
   文法が読めないテキストは、捨てずに `ParseError` にします。
4. あとで定義されるモデルかサブ回路を指していたために後回しにした行（下の「後回しにする素子」）を、もう一度解析します。
5. 素子どうしの参照の解決。`F` と `H` の制御電源、`K` のインダクタです。
6. `.CONTROL` のブロックのコマンド。

```mermaid
flowchart TD
  A[preprocess_includes] --> B[preprocess_lib_sections] --> C[preprocess_continuations]
  C --> P[read_global_definitions] --> D[preprocess_conditionals] --> E[preprocess_control_blocks]
  E --> G["PEGTL による処理：行ごとに process_line"]
  G -->|ディレクティブ| H["parse_*_directive"]
  G -->|".SUBCKT の中"| I["本体をテキストのまま保存"]
  G -->|素子| J["parse_*_device"]
  J -->|"UnresolvedDefinition"| K["後回しにした素子をもう一度解析"]
  K --> L["F と H の制御電源、K のインダクタを解決"]
  L --> N["execute_control_commands"]
```

ディレクティブは `.SUBCKT` の本体かどうかを調べる前に処理するので、サブ回路の中の `.MODEL` はグローバルに
なります（モデル名の 2 つ目の定義は警告で名指しします）。そこにある `.PARAM` は本体と一緒に保存し、インスタンスごとにその中だけで効きます。`.SUBCKT` の中の素子の行はテキストのまま保存し、展開のときに同じ素子のパーサーで解析します（[回路](#回路)）。入れ子の
`.SUBCKT` の定義はエラーです。パーサーが読まないディレクティブは、1 回警告して無視します（`.NOISE`、
`.MEASURE`、`.TITLE` は未対応として）。読んだディレクティブは `SimulationConfig::directives_read` に
記録し、プログラムはそこから、実行しないものを名指しします。`BCDEFGHIKLMOQRSTVXYZ` 以外の文字の素子は
`ParseError` です（`is_element_letter`）。

**使う前に定義されていないモデルとサブ回路。** ダイオード、BJT、MOSFET、MESFET の解析は、モデルがまだ
分からないと、`X` の解析はサブ回路がまだ分からないと、`UnresolvedDefinition` を投げます。`process_line` は
その行を保存し、PEGTL の処理の後で保存した行をもう一度解析します。そこでも定義がなければ `ParseError` です。

**式**は ExprTk を使い、4 か所でそれぞれ別の記号表を用意します。中括弧の素子の値（`ExpressionEvaluator` を
通した `read_element_value`）、`.PARAM`（`evaluate_param_value`。すべての `.FUNC` を登録します。`read_global_definitions` が
グローバルの `.FUNC` と `.PARAM` の行をほかのどの行よりも先に読み、定義を解きます）、サブ回路の
パラメータ（`SubcircuitDefinition::substitute_parameters`。グローバルの `.PARAM` の値の上に、サブ回路の既定値、
インスタンスの値、本体の `.PARAM` の行が重なります）、
`B` 電源（`CircuitGraph`）です。
はじめの 3 つは同じ定数 `pi` と `e`（`util::kExpressionConstants`）を知っていて、ExprTk と同じく、名前の
大文字と小文字を区別しません。

結果は `NetlistParseResult { std::unique_ptr<CircuitGraph>, SimulationConfig }` です。`SimulationConfig`
（`include/kirimaspice/core/types.hpp`）は、解析、オプション、モデルを持ちます。`find_model` は大文字と
小文字を区別せずにモデルを引きます。

## 回路

`CircuitGraph`（`src/circuit/circuit_graph.cpp`）は、ノードとデバイスを持ちます。

- ノードは 0 から番号を付けます。ノード 0 がグラウンドで、名前は `0` です。`is_ground_name` は、大文字と
  小文字を問わず `gnd` もそこへ対応させます（[NETLIST_CONVENTIONS](spec/NETLIST_CONVENTIONS.ja.md) の
  規則 5）。
- ノードの名前は `add_node`、`get_node_id`、`has_node` で小文字にそろえるので、大文字小文字を区別せずに
  比べます。出力の書き手は、`include/kirimaspice/output/signal_names.hpp` を通して、信号を小文字の
  `v(<ノード>)`、`i(<電源>)` と名付けます。
- `unknown_name` は行の未知数を `V(<ノード>)` または `I(<電源>)` と名付けます。KLU が行列を特異と
  判定すると、ソルバーはその列を `solvers::factorization_failure` を通して
  `singular matrix: check V(b)` のように報告します。
- デバイスは `std::shared_ptr<Device>` として、名前と順番で持ちます。
- 電流が方程式の未知数になるデバイスは、`voltage_sources_` にも入れ、その中の位置を枝の番号にします。
  独立電圧源（DC と波形）、電圧の形の `B` 電源、`E`、`H` です。インダクタには枝がなく、DC では 10⁶ S の
  コンダクタンスです。
- ノード k は方程式の k − 1 行目、枝 b は (ノードの数 − 1) + b 行目です。系の大きさは、グラウンドを除いた
  ノードの数と枝の数の和です。
- Boost.Graph の `adjacency_list` は、ノードごとの頂点とデバイスごとの辺を受け取りますが、どこからも
  読まれません。

`expand_all_subcircuits` は、サブ回路の定義を並べ、循環がないかを調べ（`SubcircuitRegistry`）、展開した
`X` の行がさらにインスタンスを足すことがあるので、なくなるまでインスタンスを展開します。
`expand_subcircuit_instance` は、パラメータを置き換え、ピンを外側のノードに対応させ、グラウンドと
グローバルなノードはそのまま残し、内側のノードとデバイスに `X1.n`、`X1.R1` という名前を付けます。本体の
行は、パーサーが回路に渡した `SubcircuitLineReader`（パーサーの `BodyLineReader`）に渡します。
`rename_body_line` が素子、そのノード、素子が参照するほかの素子の名前を変え、最上位と同じ素子のパーサー
`parse_element` が、ネットリスト全体のモデルとパラメータで行を読みます。展開の後に、追加した `F` と `H` の
制御と `K` のインダクタを解決します。

## デバイス

デバイスはすべて `Device`（`include/kirimaspice/devices/device.hpp`）から派生します。

| メソッド | 使い道 |
| --- | --- |
| `nodes()` | 端子 |
| `is_linear()`、`is_dynamic()` | ソルバーがどの書き込みを呼ぶか |
| `stamp_dc_linear`、`stamp_dc_nonlinear`、`stamp_gmin` | DC。線形の部分、今の解での Newton 法の線形化、gmin のコンダクタンス |
| `stamp_transient(mna, v, dt, method, ...)` | 過渡解析。積分法のコンパニオンモデル |
| `stamp_ac`、`linearize_at` | AC。動作点での小信号の書き込み |
| `save_state`、`restore_state`、`reset_state`、`accept_solution` | 過渡解析の履歴。ステップごとに残すか戻す |
| `get_charge`、`get_flux`、`get_physical_lte` | 電荷と磁束からの局所打ち切り誤差 |
| `get_noise_sources` | ノイズ |
| `get_parameter`、`set_parameter`、`get_perturbable_parameters` | 感度、ワーストケース、最適化 |

書き込み（スタンプ）はデバイスの線形化したコンパニオンを書くので、ソルバーは補正量ではなく新しい解に
ついて G·x = b を解きます。書き込みは `const` のメソッドで、必要な履歴は `mutable` のメンバーに持ちます。

温度はインターフェースに入っていません。温度に依存するモデルはそれぞれ `set_temperature` を持ち、
`apply_temperature_to_devices`（`src/solvers/temperature_utils.cpp`）が種類ごとにそれを呼びます。
`DCSolver::solve`、`DCSolver::solve_sweep`、`TransientSolver::solve` は、始めに
`SimulationConfig::temperature`（`.TEMP` か `.OPTIONS TEMP` がなければ 27 °C）でこれを呼ぶので、`.TEMP` が
なくても、すべての解析が回路の温度でデバイスを評価します。ほかの解析は動作点を `DCSolver::solve` から
取ります。レベル 3、MESFET、SCR、TRIAC、抵抗以外の受動素子はそこに入っておらず、この関数は飛ばします。
ダイオード、BJT、レベル 1 の MOSFET、抵抗は、`set_parameter` で設定したパラメーターを TNOM での値として
持ち、今の温度に合わせて変えます。

| 種類 | ファイル |
| --- | --- |
| 受動素子、結合、線路 | resistor、capacitor、inductor、mutual_inductance、tline_lossless、tline_lossy |
| 電源 | voltage_source、current_source（どちらも `waveform.hpp` の波形と一緒に）、behavioral_source、vcvs、vccs、cccs、ccvs |
| ダイオードとバイポーラ | diode、bjt（bjt_npn と bjt_pnp は別名） |
| MOSFET | mosfet_base、mosfet_level1、mosfet_level3、mosfet_bsim1、mosfet_bsim3（bsim3_core と一緒に）、mosfet_bsim4（bsim4_core とそのパラメータと一緒に）、mosfet_sic、finfet |
| パワー素子とそのほか | gan_hemt、vdmos、igbt、jfet、mesfet、scr、triac |

MOSFET のモデルは、`Polarity`（`polarity.hpp`）についてのクラステンプレートです。GaN HEMT、IGBT、JFET、
MESFET、SCR、VDMOS は、それぞれ自分の極性の列挙型（`GaNPolarity` など）についてのテンプレートで、ヘッダーに
だけあります。レベル 1、レベル 3、BSIM1、BSIM3、BSIM4、SiC、FinFET は、それぞれの `.cpp` で明示的に
実体化します。バイポーラトランジスタは `BJT<BJTPolarity>` で、`bjt.cpp` で NPN と PNP を実体化します。
`BJT_NPN` と `BJT_PNP` はその別名です。DC、AC、過渡、ノイズ解析の電流と解析的な微分は
`BJT::evaluate` から取り、容量は接合の電荷から求めます。ライブラリには JFET がありますが、パーサーに
`J` 素子はありません。

## 方程式と線形ソルバー

- `SparseMatrix`（`include/kirimaspice/core/matrix_types.hpp`）は (行, 列, 値) の並びです。`add_entry` は
  0 も残すので、回路の非零の形は反復の間で変わりません。`to_csc` は重複を足し合わせて、KLU が受け取る
  圧縮列形式にします。`MNASystem` は G、b、x を持ち、`ComplexMNASystem` はその AC 版です。
- ソルバーは `ISparseLinearSolver` を通して線形ソルバーを使います。`choose_backend`
  （`src/solvers/solver_factory.cpp`）は、ライブラリの呼び出し側が設定した上書き（`set_cli_backend_override`。
  単体テスト 1 つとベンチマーク 1 つが使う）、ビルドの既定の順に選びます。プログラムは何も設定せず、
  環境変数は読みません。動く
  バックエンドは KLU だけで、`SuperLUDISTSolver` はどのメソッドも `false` を返します。
- Newton 法のループでは、最初の反復で解析と分解を行い、非零の形が変わらない間は、あとの反復で保存した
  記号解析を使って分解し直します。
- `ParallelDeviceStamper` は、OpenMP のスレッドでデバイスをそれぞれ別の系に書き込み、まとめます。DC では
  デバイスが 10 000 個以上のときに使い、過渡解析のしきい値 1 000 000 は、実際には使わないのと同じです。

## DC 解析

図は `solve_standard` の主な反復です。この経路とステッピング・継続法への振り分けは後述し、
ほかの経路の内部の反復は図には示していません。一歩の制限と動的 gmin は省略していますが、
収束判定には変化量とデバイス電流の釣り合いを含みます。

```mermaid
flowchart TD
    I["初期推定値 x_old"] --> Q{"反復回数が残っているか"}
    Q -->|はい| S["デバイスを G と b に書き込む"]
    Q -->|いいえ| F["MAX_ITERATIONS"]
    S --> L["分解して G x_new = b を解く"]
    L --> V{"分解と求解に成功したか"}
    V -->|いいえ| E["SINGULAR_MATRIX / NUMERICAL_ERROR"]
    V -->|はい| C{"check_convergence"}
    C -->|はい| R["解を保存: SUCCESS"]
    C -->|いいえ| U["x_old = x_new として次の反復へ"]
    U --> Q
```

`DCSolver::solve`（`src/solvers/dc_solver.cpp`）は、オプションで求められていれば、この優先順で電源
ステッピング、gmin ステッピング、継続法を行い、そうでなければ Newton 法のループ `solve_standard` を行います。
`PSEUDO_TRANSIENT=1` のときは、それが失敗すると疑似過渡法に切り替えます（既定では切り替えません）。

- **初期推定値。** 0 と、`.NODESET` が名指すノードではその値です（`DCSolver::initialize_solution`）。
  デバイスの種類やつながり方からは何も推定しません。`.NODESET` があると、`DCSolver::solve` は、SPICE と
  同じく、まずそのノードをその値に固定して解き（`.IC` がノードを固定するのと同じ）、その点から固定を外して
  解き続けます。そのため、動作点がいくつもある回路で、`.NODESET` がその一つを選びます。
- **Newton 法のループ。** 書き込みから系を組み立て、分解し、解き、収束を調べます
  （`DCSolver::check_convergence`）。
- **収束。** まず一歩の大きさを調べます。ノードが不合格になるのは、変化が `vntol` と、値の `reltol` 倍の
  両方を超えたときだけです（枝の電流は `abstol`）。次に、新しい解でキルヒホッフの電流則を調べます
  （`kirchhoff_current_law_holds`）。各デバイスをそこで空の系に書き込むと、その寄与
  G<sub>d</sub>·x − b<sub>d</sub> はデバイス自身の電流になります。各行の和は、その行で最大の寄与の
  `reltol` 倍に `abstol`（枝の行は `vntol`）と、丸めの分として項の大きさの 10<sup>−12</sup> 倍を加えた
  値より小さくなければなりません。反復で解いた gmin（gmin ステッピング、動的 gmin）と固定したノードも
  寄与に含めます。gmin ステッピングと動的 gmin は、グラウンドへのシャントを除いた回路の求解で終わるので、
  グラウンドへの経路のない回路は、これらがあってもなくても失敗します。過渡解析の 1 ステップの Newton 反復も、そのステップの各デバイスの `stamp_transient` で
  同じ判定をします（`TransientSolver::check_convergence`）。そのため、書き込みはデバイスの履歴を変えては
  いけません。履歴を進めるのは `accept_solution` です。
- **制限。** Newton 法の一歩全体は制限しません。SPICE と同じく、デバイスが自分の電圧を制限します。
  ダイオードとバイポーラトランジスタ（NPN と PNP）は、反復の間に接合電圧が動く幅を
  制限し（`core::limit_junction_step`。SPICE と同じです）、IGBT は V<sub>CE</sub> を SPICE3 の FET の
  V<sub>DS</sub> の規則で制限します（`core::limit_fet_drain_step`）。レベル 1 の MOSFET と MESFET は、V<sub>GS</sub> を
  SPICE3 の FET のゲートの規則で（`core::limit_fet_gate_step`。逆モードでは V<sub>GD</sub>）、V<sub>DS</sub> をドレインの
  規則で制限します。制限した書き込みから得た解は収束と
  見なしません（`any_device_limited`）。制限の基にする電圧は、`reset_state`、`save_state`、
  `restore_state` が扱う状態に含まれます。
- **DC スイープ。** 入れ子の `.DC` では、最初の電源が内側のループです
  （[NETLIST_CONVENTIONS](spec/NETLIST_CONVENTIONS.ja.md) の規則 4）。点は 1 つずつ順に解き、それぞれ
  前の点からではなく、初期の推定値から始めます。

## 過渡解析

`TransientSolver::solve`（`src/solvers/transient_solver.cpp`）は、回路の温度（既定は 27 °C）を設定し、始点を
求め、時間を進めます。

- **始点。** `UIC` があれば `initialize_uic_mode` が `.IC` と、キャパシタとインダクタの `IC=`
  （`apply_initial_condition`）から始めます。なければ始点は動作点で、`.IC` の
  ノードをその値に固定して解き（`DCSolver::hold_node_voltages`。それぞれ 10¹⁰ S のコンダクタンス）、
  過渡解析ではそれを外します。
- **方法**（`IntegrationMethod`）。後退オイラー法、台形則（既定）、BDF2（`gear`、`gear2`、`bdf2`）、
  BDF3（`gear3`、`bdf3`）、TR-BDF2 で、`.OPTIONS METHOD=` で選びます。各デバイスの `stamp_transient` が、
  その方法のコンパニオンモデルを書きます。ダイオード、BJT、BSIM3、BSIM4、IGBT は容量を
  `device_utils::capacitor_companion` を通して積分し、SiC MOSFET、GaN HEMT、VDMOS、FinFET、MESFET は、
  必要な履歴を持つ `device_utils::CapacitorHistory` を通して積分します。BSIM4 の `CAPMOD=2` は
  端子電荷にも共通の積分則を適用し、ステップを棄却したときに戻せるよう、受理した電荷と電流を保存します。
- **固定刻み。プログラムが使う経路です。** 刻みは TSTEP と TMAX の短いほうです（`main.cpp`。TMAX が
  ないか 0 ならパーサーが TSTEP にします）。`run_timestepping_loop` は電源の折れ点（`Waveform::breakpoints`。
  `PULSE` の角、`PWL` の点、`SIN` と `EXP` の遅れ）ごとに区間を区切るので、そこに必ず点があります。
  その後、刻みは格子に戻り、積分はそこから後退オイラー法の 1 ステップでやり直します（`history_count_`
  を戻します）。最初のステップ（BDF2 では最初の 2 ステップ）は後退オイラー法で進み、BDF3 は、履歴が 1 点か 2 点の間は
  BDF2 のステップで進みます。失敗したステップは、後退オイラー法でやり直し、それでも駄目なら刻みを半分にして、
  15 回までやり直します。打ち切り誤差による制御はありません。

  ```mermaid
  flowchart LR
    S["その方法で 1 ステップ"] -->|収束| OK["受け入れて次のステップへ"]
    S -->|失敗| B["同じ刻みで後退オイラー法"]
    B -->|収束| OK
    B -->|失敗| Q{"15 回半分にしたか、<br/>刻みが最小か"}
    Q -->|いいえ| H["刻みを半分にする"] --> S
    Q -->|はい| E["過渡解析は失敗"]
  ```
- **可変刻み。ライブラリの中だけです。** `solve_timestep_adaptive` は、次数の違う 2 つの方法の組か、
  予測子と修正子から局所打ち切り誤差を見積もり、20 歩受け入れた後はデバイスの `get_physical_lte` も
  使います。次数も変えられます。この経路を使うネットリストのオプションはありません。
- **ステップの受け入れ。** `accept_timestep_solution` は、要素 0 をグラウンドにしたノードの電圧を、
  すべてのデバイスの `accept_solution` に渡します。相互インダクタンスは、そのインダクタの後、最後に
  受け入れます。
- **TR-BDF2** は、電源を t + γ·Δt で取る γ·Δt（γ = 2 − √2）の台形則の段と、Δt までの BDF2 の段を進めます。
  伝送線路は第 1 段の受理を無視するので、その時計は時刻の点にとどまります。

## AC 解析

動作点は全周波数で共通です。枠の中は周波数ごとに独立して実行し、OpenMP があれば分担できます。

```mermaid
flowchart TD
    D["DC 動作点を解く"] --> Q{"DC が収束したか"}
    Q -->|いいえ| E["AC 解析は失敗"]
    Q -->|はい| L["linearize_at: 動作点でデバイスを線形化"]
    L --> F["周波数の点を生成"]
    subgraph EACH["周波数ごとの処理"]
        S["stamp_ac: 大きさ n の複素数の系"]
        S --> C["大きさ 2n の実数の系へ変換"]
        C --> K["ISparseLinearSolver を通して解く"]
        K --> P["その周波数の複素数の解を得る"]
    end
    F --> S
    P --> R["点と失敗状態を ACResult にまとめる"]
```

`ACSolver::solve`（`src/solvers/ac_solver.cpp`）は、動作点を解き、そこでデバイスを線形化し
（`linearize_at`）、周波数ごとに `stamp_ac` を複素数の系に書き込みます。大きさ n の複素数の系は、
大きさ 2n の実数の系 [Re −Im; Im Re] として、実数のバックエンドで解きます。周波数は OpenMP で並列に、
それぞれ別のソルバーで解きます。入力は `AC` を書いた電源です（[NETLIST_CONVENTIONS](spec/NETLIST_CONVENTIONS.ja.md)
の規則 2）。

## ライブラリにあるほかの解析

| 解析 | 場所 | ネットリストから使えるか |
| --- | --- | --- |
| フーリエ（`.FOUR`） | 過渡解析のソルバーが呼ぶ `FourierAnalyzer` | 使える。`.TRAN` と一緒に |
| 温度スイープ（`.TEMP`） | DC と過渡解析のソルバーの `solve_temp_sweep` | `.TEMP` は使えるが、これらの関数は通らない。プログラムが `.TEMP` の値ごとに自分で解析を実行する（`main.cpp` の `run_analyses`） |
| ノイズ | `NoiseSolver`。`get_noise_sources` についての随伴法を、AC 解析の掃引の周波数（`solvers::ac_frequency_points`）で解く | 使えない。`.NOISE` がない |
| 感度 | `SensitivityAnalyzer`。`set_parameter` による差分 | 使えない。`.SENS` は解析する |
| 伝達関数 | `TransferFunctionSolver`。入力の電源についての差分 | 使えない。`.TF` は解析する |
| 極と零点 | `PoleZeroSolver`。仮の実装 | 使えない。`.PZ` は解析する |
| ひずみ | `DistortionSolver`。小信号の大きさの比 | 使えない。`.DISTO` は解析する |
| S パラメータ | `SParameterSolver`。`TouchstoneWriter` と一緒に | 使えない。`.PORT` は解析する |
| パラメータスイープ（`.STEP`） | `ParameterSweepExecutor`。点ごとにネットリストを解析し直す | 使えない。`.STEP` は解析する |
| モンテカルロ、ワーストケース | `MonteCarloExecutor`（各回のシードは、基のシードと回の番号から `run_seed` が作る）、`WCaseAnalyzer`（公称はすべて typical の場合）（`src/analysis`） | 使えない |
| 最適化、BSIM4 の抽出 | NLopt を使う `CircuitOptimizer`、`BSIM4ParameterExtractor` | 使えない |
| 測定 | `MeasureEngine`（`src/measure`） | 使えない。`.MEASURE` は警告して無視する |

これらはライブラリの呼び出しとしてテストしています。プログラムが解析しても実行しないディレクティブは
[KNOWN_ISSUES](../KNOWN_ISSUES.ja.md#ネットリストの入力) にあります。

## 設計がまだ意図どおりでない箇所

- **BSIM4 は、ノイズのために電流をもう一度計算します。** デバイスの式は 1 か所にだけ書くという決まりに
  反しています（[KNOWN_ISSUES](../KNOWN_ISSUES.ja.md#デバイスモデル)）。ほかの解析は `BSIM4Core` を通して
  BSIM4 を評価しますが、`MOSFET_BSIM4::get_noise_sources` は代わりに簡略化した関数を使い、
  `BSIM4ParameterExtractor` も同じです。SiC MOSFET、GaN HEMT、VDMOS は、DC、AC、ノイズのどれでも、
  電流を 1 つの `drain_current` で評価し、IGBT は `collector_current` で評価します。
- **キャパシタ、インダクタ、相互インダクタンスは、コンパニオンモデルの式を自分で書いています。** ダイオード、
  BJT、BSIM3、BSIM4、IGBT が使う `device_utils::capacitor_companion` を使いません。数値は同じです
  （[KNOWN_ISSUES](../KNOWN_ISSUES.ja.md#ソースコード)）。
- **温度を種類ごとに設定します。** `apply_temperature_to_devices` は、一覧にない種類を飛ばします
  （[デバイス](#デバイス)を参照）。
- **Newton 法のループを何度も書いています。** `solve_standard`、電源ステッピングと gmin ステッピング、
  過渡解析のステップ、TR-BDF2 の 2 つの段です。
- **ExprTk を 4 回、別々の記号表で用意しています。** そのため、式から見える名前が、書いた場所によって
  違います。
- **ソルバーが `const CircuitGraph&` のデバイスを変えます。** スイープの電源の値、温度、過渡解析の始めの
  状態のリセットです。続けて実行する解析は、その状態を共有します。
- **静的な状態があります。** ソルバーの工場は `set_cli_backend_override` などで設定した上書きを静的変数に持ち、`bsim4_core.cpp` と
  `mutual_inductance.cpp` のいくつかの関数は、OpenMP が並列に実行しうる経路で関数内の静的変数を持ちます。
- **使われていない部分。** `CircuitGraph` の Boost.Graph、BSIM4 が `extra_node_count` で宣言する内部ノード
  （ソルバーは 0 個として扱います）、SuperLU_DIST のバックエンド、止めてある DC スイープの並列化、
  `BSIM4Core::compute_cdrain` の解析的な微分、テストだけが呼ぶ `CircuitGraph::add_sin_voltage_source`、
  `add_pulse_voltage_source`、`add_pwl_voltage_source` です（パーサーは `add_voltage_source_with_waveform`
  を使います）。
