# kirimaspice の開発

[English](DEVELOPMENT.md) · [プロジェクトの README](../README.ja.md) · この文書は英語版を正とします。日本語版はその訳で、両者が食い違う場合は英語版に従います。

**2026-11-15 更新**、コミット `eaf0629` に合わせています。この手引きは、kirimaspice を変える人のための
ものです。開発用のビルド、テストの構成と実行、push の前に確かめること、プロジェクトの決まりの中で
デバイスモデルを足したり不具合を直したりする方法を書きます。受け付ける貢献は
[CONTRIBUTING](../CONTRIBUTING.ja.md) に、人にもエージェントにも等しく適用される決まりは
[CLAUDE.md](../CLAUDE.ja.md) に、コードの説明は [ARCHITECTURE](ARCHITECTURE.ja.md) にあります。

## 目次

- [開発用にビルドする](#開発用にビルドする)
- [テスト](#テスト)
- [push の前に](#push-の前に)
- [継続的インテグレーション](#継続的インテグレーション)
- [デバイスモデルを足す、変える](#デバイスモデルを足す変える)
- [不具合を直す](#不具合を直す)
- [期待値と参照データ](#期待値と参照データ)
- [ベンチマーク](#ベンチマーク)
- [デバッグの助け](#デバッグの助け)

## 開発用にビルドする

コマンドは [README](../README.ja.md#ビルド) と同じです。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- ビルドディレクトリは、ソースの直下の `build/` でなければなりません。テストの一部が、そこからの相対位置で
  データを探すためです。
- SuiteSparse（KLU）はシステムのものを使います。ほかの場所に入れたものは `-DCMAKE_PREFIX_PATH=<prefix>` で
  渡します。`tools/local_ci.py` には `--cmake-arg=-DCMAKE_PREFIX_PATH=<prefix>` として渡します。
- CMake のオプション（`BUILD_TESTS`、`BUILD_BENCHMARKS`、`SANITIZE_ADDRESS`、`ENABLE_COVERAGE` など）は
  [リファレンスマニュアル](REFERENCE_MANUAL.ja.md#ビルドのオプション)にあります。
  `-DSANITIZE_ADDRESS=ON` は、夜間の AddressSanitizer のジョブが使うものです。
- macOS では、Homebrew の GCC（`-DCMAKE_C_COMPILER=gcc-16 -DCMAKE_CXX_COMPILER=g++-16`）、Homebrew の clang、
  Apple clang でビルドします。Apple clang では OpenMP なしでビルドされます。
- コードは、リポジトリの `.clang-format` で整形します。プロジェクト自身のコードのファイルは、著作権の行と
  SPDX の行で始めます。`python3 tools/check_spdx.py --add` がそれを書きます。

## テスト

### 構成

| ディレクトリ | 中身 | CTest が実行するか |
| --- | --- | --- |
| `tests/unit/` | GoogleTest のプログラム。ソースファイル 1 つに 1 つで、それぞれ 1 つのクラスか関数を調べる | する。すべてのケース |
| `tests/integration/` | ネットリストを読んで解析を走らせる GoogleTest のプログラム | する |
| `tests/validation/` | `validation/` にある ngspice の参照データとの比較と、調査用の単体のプログラム | 47 個のうち 5 個（GoogleTest）。残りの 42 個はビルドされるが実行されない |
| `tests/scripts/` | Python のテスト。Python のツール、[例題](../examples/README.ja.md)、BSIM4 の DC の簡単な確認 | する |
| `tests/netlists/` | コマンドラインと Python のテストが使うネットリスト | — |
| `tests/benchmarks/` | Google Benchmark のプログラム。`BUILD_BENCHMARKS=ON` のときだけビルドされる | しない |
| `tests/debug/`、`tests/debug_*.cpp` | デバッグ用のプログラム | しない |

GoogleTest のプログラムは `gtest_discover_tests` で登録するので、CTest はテストケースを 1 つずつ
`Suite.Name` として並べます。コマンドラインのテスト（`cli_*`。`.TEMP` の実行を調べる `cli_temperature_*`
もその中にあります）、例題（`example_*`）、Python のツール（`python_tools_*`）、`bsim4_dc_smoke`、
`MutualInductanceFixTest` は `add_test` で登録し、それぞれが CTest の 1 つのテストです。コミット `eaf0629`
では、macOS で CTest のテストは 1182 個です。

### テストの一部を実行する

```sh
ctest --test-dir build -R '^example_'          # 正規表現で選ぶ
ctest --test-dir build -R 'BSIM4' -j 8          # 並列に
build/tests/test_parser --gtest_filter='*Ground*' # GoogleTest のプログラムを直接
```

期待するデータがないテストは、失敗せずにスキップします。スキップとその理由は
[KNOWN_ISSUES](../KNOWN_ISSUES.ja.md#テスト) にあります。

### 既知の失敗

`tests/known_failures.txt` には、今失敗するテストを 1 行に 1 つ、理由をコメントにして並べます。
`tools/local_ci.py` と CI は、ここにない失敗があると失敗します。また、ここにあるテストが合格すると、
一覧から外せるように報告します。一覧に名前を足すには、期待値を変えるのと同じ承認が要ります
（[下](#期待値と参照データ)を見てください）。

## push の前に

```sh
python3 tools/local_ci.py
python3 tools/local_ci.py --history   # すべてのコミットも調べる
```

`tools/local_ci.py` は、`build/` で構成、ビルド、テストを行い（Release、Ninja）、失敗を
`tests/known_failures.txt` と比べ、`tools/check_banned.py` と `tools/check_spdx.py` を実行します。
要約を表示し、ログを `build/local-ci/` に書きます。変更を報告するときは、要約を付けてください。

`tools/check_banned.py` は、公開してはいけないものを探します。ホームディレクトリへのパス、GitHub の
noreply 以外のメールアドレス、半導体ファウンドリの名前、バイナリや大きなファイル、`--history` を付けると
noreply でないコミットの作者です。出てはいけない名前は、リポジトリに書けません。追跡しないファイルに
1 行に 1 つ書き、`--private <ファイル>` か環境変数 `KIRIMASPICE_PRIVATE_PATTERNS` で渡します。

## 継続的インテグレーション

| ワークフロー | いつ | ジョブ |
| --- | --- | --- |
| `.github/workflows/ci.yml` | `main` への push、または手動 | Ubuntu 24.04 で GCC と clang による `tools/local_ci.py`。`tools/check_banned.py --history` による履歴の検査 |
| `.github/workflows/nightly.yml` | 毎日 18:17 UTC、または手動 | AddressSanitizer 付きの `tools/local_ci.py`。ngspice を入れた `tools/local_ci.py`（ngspice との比較が実行される） |

ワークフローは GitHub の標準のランナーで動き、セルフホストのランナーは使いません。ジョブの公開ログに、
実行したマシンの名前が出るためです。履歴の検査の非公開パターンは、リポジトリのシークレット
`PRIVATE_PATTERNS` から取ります。

## デバイスモデルを足す、変える

デバイスの式は 1 か所にだけ書きます。どの解析（動作点、DC スイープ、AC、過渡、ノイズ）も、同じモデルの
コードを通してデバイスを評価するので、1 つの解析のために式の写しを足さないでください。まだそうなって
いない箇所（BSIM4 のノイズ源と、IGBT の過渡解析の書き込み）は、
[KNOWN_ISSUES](../KNOWN_ISSUES.ja.md#デバイスモデル) に書いてあります。新しいコードで、そのような箇所を
増やしてはいけません。`tests/unit/test_small_signal_consistency.cpp` は、デバイスの AC の利得が DC の
伝達特性の傾きに等しいことを調べます。新しいデバイスには、そこにケースを足してください。

手順です。クラスとファイルは [ARCHITECTURE](ARCHITECTURE.ja.md#デバイス) で説明しています。

1. `include/kirimaspice/devices/` と `src/devices/` にデバイスを実装します。デバイスの基底クラスから派生し、
   そのデバイスが加わる解析の書き込み（スタンプ）を実装します。
2. 回路（`src/circuit/` の `CircuitGraph`）に足します。ネットリストから使えるようにするなら、パーサー
   （`src/parser/netlist_parser.cpp`）とサブ回路の展開にも足します。
3. テストを書きます。期待値は、モデルの仕様か解析解、または版を記録した ngspice の実行から決めます。
4. [リファレンスマニュアル](REFERENCE_MANUAL.ja.md#素子)に素子を書きます。

新しいデバイスが関わる場所です。

| 場所 | 足すもの |
| --- | --- |
| `include/kirimaspice/core/types.hpp` | 極性ごとの `DeviceType` の値 |
| `include/kirimaspice/devices/`、`src/devices/` | クラス。ヘッダーだけでないテンプレートなら、`.cpp` のファイルでの明示的な実体化 |
| `CMakeLists.txt` | `kirimaspice_lib` のソースへの `.cpp` のファイル（ソースはグロブで集めず、列挙しています） |
| `src/parser/netlist_parser.cpp` | 素子、または `.MODEL` カードの `LEVEL` と、そのパラメータ |
| `CircuitGraph::expand_subcircuit_instance`（`src/circuit/circuit_graph.cpp`） | `.SUBCKT` の中で使えるようにするなら、その中の素子 |
| `apply_temperature_to_devices`（`src/solvers/temperature_utils.cpp`） | 温度に依存するなら、デバイスの `set_temperature` の呼び出し |
| `tests/unit/`、`tests/CMakeLists.txt` | テストとその登録 |
| `tools/check_spdx.py` の `ECL_FILES`、[THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.ja.md) | BSIM のモデルのコードに基づくコードのときだけ |
| [リファレンスマニュアル](REFERENCE_MANUAL.ja.md#素子)（英語版と日本語版） | 素子、そのパラメータ、その既定値 |

BSIM のモデルのコードに基づくコードは、その告知（ECL-2.0）を残し、`tools/check_spdx.py` の `ECL_FILES` の
SPDX の式を付け、[THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.ja.md) に載せます。GPL や LGPL の
ソースコードは読まず、移植もしません。ngspice のファイルは、読む前にそれぞれのライセンスを確かめます
（[CLAUDE.md](../CLAUDE.ja.md#gpl-や-lgpl-のコード)）。

## 不具合を直す

症状の形を条件にしないでください。テストの回路、ノード名、ファイル名、issue の番号を `if` に書きません。
不具合を直すコミットには、次を書きます。

1. 破っていた規則。モデルの仕様の節、または解析解。
2. その規則を実装している 1 か所。関数の名前で。
3. 同じ場所を通るほかの経路。

シミュレータは、ファイル名やパスで振る舞いを変えず、テストのことを知りません。

## 期待値と参照データ

- 期待値は、モデルの仕様、解析解、または版を記録した ngspice の参照の実行から決めます。実装そのものから
  決めることはありません。
- テストを通すために、期待値やテストを書き換えないでください。実装と期待値が食い違うときは、まず、
  どちらが仕様や解析解に従っているかを人に見せます。
- 既存の期待値を変える、消す、緩める変更、テストを消すか名前を変える変更、`tests/known_failures.txt` に
  テストを足す変更は、`src/` の変更とは別のコミットにし、理由を書き、人の承認を得て、それを
  `Approved-by` のトレーラーに記録します。新しいテストとその期待値は、実装と一緒でかまいません。
- `validation/` の参照データは、版を記録していない ngspice の実行から作られました。新しい参照データには、
  ngspice の版、コマンド、ネットリストを記録します。`scripts/` のスクリプトは、ngspice を実行して BSIM4 の
  参照データを作ります。

  | スクリプト | 書き出すもの |
  | --- | --- |
  | `scripts/bench/generate_bsim4_bias_refs.py` | `/tmp/bsim4_bias_refs.json`、13 のバイアス点 |
  | `scripts/bench/generate_bsim4_temperature_refs.py` | `/tmp/bsim4_temperature_refs.json`。`BSIM4TemperatureValidationTest` が読む |
  | `scripts/generate_bsim4_tran_ref.py` | `validation/bsim4_inv_chain_tran_ref.txt`。`test_bsim4_transient.cpp` が読む |

- 商用シミュレータの出力、ログ、波形は使いません（[FAQ](FAQ.ja.md#商用シミュレータと比べていますか)）。

## ベンチマーク

```sh
cmake -S . -B build-bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
cmake --build build-bench --target bench_device_stamping
```

`BUILD_BENCHMARKS=ON` にすると、Google Benchmark を取ってきて、`tests/benchmarks/` のプログラムをビルド
します。`bench_large_circuit`、`bench_solver_backends_ac`、`bench_sparse_refactorize`、
`bench_sparse_matrix_zero`、`bench_device_stamping`、`bench_dc_thread_profile` です。CTest は実行せず、CI は
ビルドせず、結果は公開していません。

## デバッグの助け

- プログラムは `info` のレベルでログを出します（`src/main.cpp`）。レベルを変えるオプションはありません。
  コードの中の `debug` と `trace` のメッセージを出すには、`main.cpp` を変える必要があります。
- `KIRIMASPICE_DEBUG_DUMP` は、どんな値でも設定すると、DC の解を求めるたびに、Newton 法の 2 回目の反復の
  MNA の系を、今のディレクトリの `debug_coo.txt`、`debug_csc.txt`、`debug_b.txt`、`debug_x.txt` に書きます。
  `KIRIMASPICE_BSIM1_DEBUG` は BSIM1 のデバッグログを出します。値が空でないときは、その名前のデバイスに
  ついてだけ出します（[リファレンスマニュアル](REFERENCE_MANUAL.ja.md#環境変数)）。
- `--raw-out` は、動作点、DC スイープ、過渡解析の、すべてのノードの電圧と枝の電流を書きます。
  [Python のツール](USER_MANUAL.ja.md#python-のツールを使う)で読めます。
