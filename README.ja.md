[English](./README.md) / **日本語**

# MikuMikuWorldEX

sevenc-nanashiによる[MikuMikuWorld4CC](https://github.com/sevenc-nanashi/MikuMikuWorld4CC)をベースに、crash5bandによるオリジナルの[MikuMikuWorld](https://github.com/crash5band/MikuMikuWorld)のプレビューシステムを統合したプロジェクトセカイ用の譜面エディターです。

## 概要

MikuMikuWorldEXは、MikuMikuWorld4CC（Chart Cyanvasフォーク）の拡張譜面機能と、オリジナルのMikuMikuWorldのリアルタイムプレビューシステムを組み合わせています。これにより、譜面制作者は正確なノーツ描画、パーティクルエフェクト、音声同期を用いて譜面をプレビューできます。

## 機能

MikuMikuWorld4CCの全機能をサポート：

- ダメージノーツ
- 拡張レーン
- ガイドの色付け（8色）
- 新しいイージング（EaseInOut、EaseOutIn）
- ハイスピードの補間
- スライドの中継点の繰り返し
- ノーツプロパティパネル
- 小数点のノーツレーンと幅のサポート

MikuMikuWorldEXの追加機能：

- パーティクルエフェクトと音声付きのリアルタイムプレビュー
- ダメージノーツやカラーガイドを含むすべてのMMWCCノーツタイプをプレビューでサポート

## 今後の予定

- エディターの利便性向上
- NEXTSekai譜面のサポート

## クレジット

- [crash5band](https://github.com/crash5band) - オリジナルのMikuMikuWorldとプレビューシステム
- [sevenc-nanashi](https://github.com/sevenc-nanashi) - 拡張機能を備えたMikuMikuWorld4CCフォーク

### 翻訳（MikuMikuWorld4CCより）

- Español（スペイン語）：@mi.honesta.reaccion
- Русский（ロシア語）：@\_notfallen\_
- Tiếng Việt（ベトナム語）：@uwulovecrosshand
- 한글（韓国語）：@fjordic
- Türkçe（トルコ語）：@sctech-tr
- Português do Brasil（ブラジルポルトガル語）：@\_\_noradrenaline

## 必要な環境

- 64bitのWindows 10以降
- OpenGL 3.3対応のGPUと最新のドライバ

古いバージョンのWindowsでも動作する可能性がありますが、公式にはサポートしていません。

## ダウンロード

最新版は[Releases](https://github.com/vewaxio/MikuMikuWorldEX/releases)ページからダウンロードできます。

## ビルド

Visual Studio 2019以降とC++デスクトップ開発ワークロードが必要です。

1. リポジトリをクローン
2. `MikuMikuWorld.sln`を開く
3. Release x64構成でビルド

## ライセンス

詳細は[LICENSE](./LICENSE)を参照してください。
