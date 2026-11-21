# kirimaspice 文書の案内

[English](README.md) · [プロジェクトの README](../README.ja.md) · この文書は英語版を正とします。日本語版はその訳で、両者が食い違う場合は英語版に従います。

**2026-11-15 更新**、コミット `eaf0629` に合わせています。したいことから文書を選んでください。ガイドと
マニュアルには、確かめたコミットが冒頭近くに書いてあり、KNOWN_ISSUES と例題には、測ったときのコミットが
書いてあります。文書とプログラムが食い違うときは、動くのはプログラムで、その食い違いはどちらかの
不具合です。結果を報告するときは、プログラムが最初に出す版の行（`kirimaspice v0.0.0`）と、ビルドした
コミットを残してください。

| 知りたいこと | まず読む | 次に |
| --- | --- | --- |
| ビルドして、最初のネットリストを動かすには？ | [ユーザーマニュアル](USER_MANUAL.ja.md) | [例題](../examples/README.ja.md) |
| オプション、素子、ディレクティブ、出力ファイルの意味は？ | [リファレンスマニュアル](REFERENCE_MANUAL.ja.md) | [用語集](GLOSSARY.ja.md) |
| ngspice 用のネットリストがある。何を変える？ | [ngspice からの移行](PORTING.ja.md) | [NETLIST_CONVENTIONS](spec/NETLIST_CONVENTIONS.ja.md) |
| この結果は正しい？ 間違っていると分かっていることは？ | [KNOWN_ISSUES](../KNOWN_ISSUES.ja.md) | [FAQ](FAQ.ja.md#結果と精度) |
| 過渡解析の設定を選び、波形の精度を確かめるには？ | [過渡解析](TRANSIENT_ANALYSIS.ja.md)（草案） | |
| Python から動かすには？ | [ユーザーマニュアル：Python のツール](USER_MANUAL.ja.md#python-のツールを使う) | |
| コードはどう組み立てられている？ | [設計](ARCHITECTURE.ja.md) | |
| コードを変え、テストし、push するには？ | [開発](DEVELOPMENT.ja.md) | [CONTRIBUTING](../CONTRIBUTING.ja.md)、[CLAUDE.md](../CLAUDE.ja.md) |
| 決まりとして合意したことは？ | [docs/spec/](spec/) | |

## kirimaspice を使う

- [ユーザーマニュアル](USER_MANUAL.ja.md)：ビルド、ネットリストの書き方、解析の選び方、結果の保存と読み方、
  Python のツール、収束、終了コード、困ったとき。
- [例題](../examples/README.ja.md)：解析解のある小さなネットリスト。CTest がそれぞれを確かめます。
- [リファレンスマニュアル](REFERENCE_MANUAL.ja.md)：コマンドライン、環境変数、終了コード、ネットリストの書式、
  素子、モデル、ディレクティブ、オプション、出力形式、ビルドのオプション。
- [ngspice からの移行](PORTING.ja.md)：同じ点、コマンドの対応、ネットリストの確認項目。
- [FAQ](FAQ.ja.md) と[用語集](GLOSSARY.ja.md)：よくある質問と、文書で使う用語。
- [KNOWN_ISSUES](../KNOWN_ISSUES.ja.md)：間違っていることや足りないことと、それぞれを測ったコミット。
- [やること](../TODO.ja.md)：それについてまだ残っている作業を、おすすめの順に並べたもの。

## kirimaspice を変える

- [開発](DEVELOPMENT.ja.md)：開発用のビルド、テスト、push の前の確認、CI、デバイスモデルの足し方、不具合の
  直し方、期待値の決まり。
- [設計](ARCHITECTURE.ja.md)：ライブラリの部品と、ネットリストがそれらを通る道筋。
- [CONTRIBUTING](../CONTRIBUTING.ja.md)：受け付ける貢献。
- [CLAUDE.md](../CLAUDE.ja.md) と [AGENTS.md](../AGENTS.ja.md)：コーディングエージェントの決まりで、人にも
  適用されます。

## 決まりと仕様

[spec/](spec/) の文書は決定事項で、それぞれに状態が書いてあります。

- [NETLIST_CONVENTIONS](spec/NETLIST_CONVENTIONS.ja.md)：SPICE の方言が分かれるところで、kirimaspice が
  ngspice に従う 5 つの規則（承認）。
- [AI_POLICY](spec/AI_POLICY.ja.md)：AI ツールの使い方と、誰が何を承認するか（承認）。

## プロジェクト

- [SECURITY](../SECURITY.ja.md)：脆弱性の報告。
- [CODE_OF_CONDUCT](../CODE_OF_CONDUCT.ja.md)：Contributor Covenant 2.1。
- [THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.ja.md)：依存とそのライセンス。
