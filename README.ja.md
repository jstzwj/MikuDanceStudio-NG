# MikuDanceStudio NG

[English](README.md) · [简体中文](README.zh-CN.md) · **日本語**

**MikuMikuDance の、その先の物語へ。**

MikuDanceStudio NG は、慣れ親しんだ MMD のアニメーション制作を、現代の描画技術と Windows・macOS・Linux のネイティブエディターへつなぐプロジェクトです。MikuMikuDance v9.32 の実装をソースコードとして再構築し、オリジナルの動作を手がかりに開発を進めています。

配布は **デスクトップ向けポータブル版のみ**（インストーラーなし）と、Android 向け APK です。現在のプレビュー `v0.0.2` は Windows x64／ARM64、Apple Silicon macOS、Linux x64、Android が対象です。毎回、全対象の検証後に [Releases](https://github.com/jstzwj/MikuDanceStudio-NG/releases) へ公開します。[ビルド・テスト・公開の手順](docs/RELEASES.md)をご覧ください。

## 私たちの物語

始まりは、8 年前のある夜でした。

私は **[Ray-MMD](https://github.com/ray-cast/ray-mmd) の作者、Rui** と、夜が明けるまで話していました。Ray-MMD の開発を続けることがなぜ難しいのか。MMD の DirectX 9 レンダラーには、どんな制約があるのか。そして、その先で実現したかったことについて。

Rui は、自分でレンダリングエンジンを作る道も模索しました。その試みに費やした時間は 5 年。MMD のエコシステムが持つ制約の中で、最終的には目指していた成果にも、納得のいく使い心地にも届きませんでした。

その歩みは、このプロジェクトの原点でもあります。レンダラーの周りには、モデルやモーション、エフェクト、長い時間をかけて作られた作品、そして手になじんだ操作があります。そのすべてを未来へつなぐには、制作の細部まで見つめる必要があります。

ここ 2 年ほどの AI の急速な進歩が、私たちに新たなきっかけをくれました。MMD の内部に踏み込み、その仕組みを調べ、実装をソースコードとして再構築できるようになったのです。アプリケーション全体を読み、手を入れ、開発を続けられる土台を、ようやく持つことができました。

**MikuDanceStudio NG は、ここから始まります。**

大切に作られた操作感を受け継ぎ、現代のグラフィックスに可能性を広げ、より多くの人が普段使っているコンピューターで制作できるようにする。そんな MMD を、一緒に作っていきたいと思っています。長年親しまれてきたモデル、モーション、エフェクト、シーンにも、これからの居場所があってほしい。

8 年前のあの夜に残った問いに、今、また取り組むことができます。

## 作っているもの

- **慣れ親しんだ制作環境。** ボーン、モーフ、IK、物理、カメラ、照明、アクセサリ、タイムライン、元に戻す／やり直しを、オリジナル MMD の動作に合わせて実装します。
- **これまでの作品を引き継ぐ互換性。** PMD／PMX モデル、VMD モーション、VPD ポーズ、PMM プロジェクトを扱い、実際のシーンと公開可能な生成テストデータで修正を検証します。
- **現代的な描画の土台。** 共通 RHI を通して DirectX 11、DirectX 12、Vulkan、ネイティブ Metal に対応し、マテリアル、セルフシャドウ、複数描画ターゲット、HDR 中間ターゲット、ポストエフェクトを支えます。
- **より多くの言語で使える UI。** 3 プラットフォーム共通の Qt エディターに 15 言語を用意しています。**ファイル → 環境設定 → 言語** から選択できます。
- **MME と Ray-MMD へ続く道。** 共通のエフェクト記述、リソース、Pass スケジューリングを整え、エフェクトごとに互換性を広げ、検証します。

## 現在の進捗

NG は開発中です。移植可能なコア、CPU シーンランタイム、クロスプラットフォーム描画の基礎にあたる **P0–P3 は完了**しています。**P4 エディターは変更後の範囲で段階的に受け入れ済み**です。異なる実 DPI のモニター間の移動テストは既知の制限として後日実施し、次は [P5：クロスプラットフォーム MME](docs/CROSS_PLATFORM_P5.md) に進みます。MME と Ray-MMD の完全な互換性についても開発を続けており、現在の対応範囲は検証済みのエフェクトのサブセットです。

| プラットフォーム | NG の描画バックエンド | 現在の開発基準環境 |
| --- | --- | --- |
| Windows | DirectX 11、DirectX 12、Vulkan | Windows 10／11、x64 |
| macOS | ネイティブ Metal、任意で Vulkan／MoltenVK | macOS 13+、Apple Silicon |
| Linux | Vulkan、X11 と Wayland に対応 | Ubuntu 22.04、x86_64 |

検証済みの範囲と残る課題は、[受け入れ状況](docs/P4_ACCEPTANCE_STATUS.md)、[移植計画](cross_platform_plan.md)、[RHI 機能表](docs/RHI_FEATURE_SUPPORT.md)に記録しています。開発版パッケージは [Portable editor ワークフロー](https://github.com/jstzwj/MikuDanceStudio-NG/actions/workflows/portable-editor.yml)から入手できます。

このリポジトリは [MikuDanceStudio](https://github.com/jstzwj/MikuDanceStudio) の旧 `next` ブランチから、コミット履歴をすべて引き継いで独立しました。元のリポジトリでは Windows 互換ブランチを維持し、NG は独自の `main` ブランチで開発を進めています。

## ソースからのビルド

C++17 コンパイラー、CMake 3.21 以降、Python 3、Widgets・Multimedia・Linguist ツールを含む Qt 6.8.3 が必要です。描画バックエンドには各プラットフォームの SDK とシェーダーツールも必要になります。DXC、Vulkan、Metal、Linux のシステム依存関係は、[ビルド・パッケージガイド](docs/DISTRIBUTION.md)を参照してください。

```sh
git clone https://github.com/jstzwj/MikuDanceStudio-NG.git
cd MikuDanceStudio-NG
cmake -S cmake/portable-editor -B out/build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/platform
cmake --build out/build --config Release --target dist --parallel 3
```

`out/dist` にアプリケーションと実行時の依存ファイルがそろいます。Windows は `out/dist/MikuDanceStudioNG.exe`、macOS は `.app`、Linux は `out/dist/bin/MikuDanceStudioNG` を起動してください。別のコンピューターへ移すときは、パッケージ全体をコピーします。ビルドファイルは `out/build`、配布アーカイブは `out/releases` に出力されます。

## 次の章を、一緒に

動きの違うシーン、読み込めないエフェクト、より自然な翻訳、あるいはパッチ。その一つひとつが、開発を前に進めてくれます。小さく再現できる例があると、日々 MMD を使う人にとって大切な細部を守りやすくなります。不具合を報告する際は、プラットフォーム、描画バックエンド、再現手順を添えてください。

[不具合の報告](https://github.com/jstzwj/MikuDanceStudio-NG/issues) · [ドキュメント](docs/README.md) · [翻訳ガイド](translations/README.md) · [リリースガイド](docs/RELEASES.md) · [バージョン](VERSION)

## 感謝を込めて

[MikuMikuDance](https://learnmmd.com/downloads/) を生み出し、多くの人に創作を始める場所を届けてくださった **樋口優さん（Yu Higuchi）** に感謝します。そして **Rui** に。Ray-MMD と、その先を探し続けた年月、8 年前のあの夜の会話に、感謝します。

モデル、モーション、エフェクト、音楽、チュートリアルの作者の皆さん。MME コミュニティと、知識や経験を分かち合ってきた皆さん。その積み重ねがあるからこそ、MMD を未来へつなぎたいと思えます。Qt、Bullet Physics をはじめ、本プロジェクトが利用するオープンソースツールのメンテナーにも感謝します。

## ライセンスと由来

MikuDanceStudio 独自のコードは [MIT ライセンス](LICENSE)で公開しています。オリジナル MMD のリソースには元の条項が適用されます。[許諾文](licenses/MikuMikuDance.txt)と[リソースの出典](res/README.md)を別途記録しています。サードパーティーのコンポーネントには、それぞれのライセンスが適用されます。
