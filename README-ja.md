# 🎵 aplay+ - シンプルなBitPerfectオーディオプレーヤー

**aplay+**は、Linux向けの軽量で高音質なオーディオプレーヤーです。🎧
ALSA（Advanced Linux Sound Architecture）を利用し、BitPerfectな再生を提供します。
シンプルな設計で、さまざまなオーディオ形式をサポートし、最高の音質を追求します！🔊

---

## ✨ 特徴

- **多彩なオーディオ形式に対応**
  - 🌟 **FLAC**: 高品質なロスレス圧縮
  - 🌊 **WAV**: 圧縮なしのクリアな音質
  - 🎶 **MP3**: 広く使われる圧縮形式
  - 🎵 **Ogg Vorbis**: 優れた圧縮と音質
  - 📱 **AAC (mp4/m4a)**: iPhoneやYouTubeで一般的な形式

- **簡単なビルド**
  Google Colabで簡単にビルド可能！ 🚀
  必要なライブラリをインストールしてすぐに使えます。

- **柔軟な再生オプション**
  - 🔀 ランダム再生
  - 📂 ディレクトリ内のファイル検索（正規表現対応）
  - 🎛️ 32ビット浮動小数点再生
  - 🐧 Linux向け最適化オプション

---

## 🛠️ インストール方法

1. **リポジトリをクローン**
   ```bash
   git clone https://github.com/yui0/aplay-.git
   cd aplay-
   ```

2. **必要なライブラリをインストール**
   ```bash
   dnf install alsa-lib-devel
   ```

3. **ビルド**
   ```bash
   make
   ```

4. **Google Colabでビルド**
   手軽にビルドしたい場合は、以下のリンクをクリック！
   👉 [Build with Colab](リンクを挿入)

---

## 🚀 使い方

基本的なコマンド形式：
```bash
./aplay+ [オプション] ディレクトリ
```

### 📋 オプション一覧

- `-h` : ヘルプメッセージを表示 📖
- `-d <デバイス名>` : ALSAデバイスを指定（例: `default`, `hw:0,0`, `plughw:0,0`） 🎚️
- `-f` : 32ビット浮動小数点再生を有効化 🔢
- `-r` : ディレクトリを再帰的に検索 🔍
- `-x` : ランダム再生を有効化 🔀
- `-s <正規表現>` : ファイル検索に正規表現を使用 🔎
- `-t <拡張子>` : ファイル形式を指定（例: `flac`, `mp3`, `wma`） 🎵
- `-p` : Linuxプラットフォーム向けに最適化 🐧

例：
```bash
./aplay+ -r -x -t flac /music
```
👉 音楽ディレクトリ内のFLACファイルをランダム再生！

---

## ⚙️ システム最適化

音質をさらに向上させるために、以下の設定を試してみてください！ 🔧

### 1️⃣ CPUガバナーを「パフォーマンス」に設定
```bash
#!/bin/sh
for CPUFREQ in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
do
  [ -f $CPUFREQ ] || continue
  echo -n performance > $CPUFREQ
done
```

### 2️⃣ ディスクスケジューラを最適化
```bash
#!/bin/sh
for FILE in /sys/block/sd*/queue/scheduler
do
  [ -f $FILE ] || continue
  echo -n none > $FILE
done
```

### 3️⃣ システムパラメータの調整
以下の設定を`/etc/sysctl.conf`に追加：
```bash
vm.dirty_ratio = 40
vm.dirty_background_ratio = 10
vm.dirty_expire_centisecs = 3000
vm.dirty_writeback_centisecs = 500
vm.overcommit_memory = 1
```

---

## 🤝 コントリビューション

このプロジェクトはオープンソースです！🌍
バグ修正や新機能の提案は大歓迎です。以下の手順で貢献してください：

1. リポジトリをフォーク 🍴
2. ブランチを作成 (`git checkout -b feature/awesome-feature`)
3. 変更をコミット (`git commit -m 'Add awesome feature'`)
4. プッシュ (`git push origin feature/awesome-feature`)
5. プルリクエストを作成 📬

詳細は[CONTRIBUTING.md](リンクを挿入)をご覧ください。

---

## 📜 ライセンス

このプロジェクトは[MITライセンス](LICENSE)の下で公開されています。
自由に使用、改変、配布してください！ 📄

---

## 🙌 クレジット

- **開発者**: Yuichiro Nakada ([@yui0](https://github.com/yui0))
- **プロジェクトページ**: [https://github.com/yui0/aplay-](https://github.com/yui0/aplay-)

高音質な音楽体験をあなたに！ 🎉
