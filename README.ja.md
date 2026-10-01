# TaikoNauts NULM Background

[English](README.md) | 日本語 | [導入ガイド](https://36536pu4jt98g3ju095jk.github.io/taikonauts-nulm-background-mod/)

TaikoNauts ModLoader 用の MOD です。NULM(`.nulm`、LMB)アニメーションを、演奏画面の上背景・下背景として流します。魂ゲージがクリアになると fever 背景に切り替わり、ゲージが下がると元に戻ります。

スキン本来の背景が描かれる位置で差し替えるので、レーン、ノーツ、スコアなどの他の要素は、そのまま手前に表示されます。

## 必要なもの

- TaikoNauts ModLoader v1.2.0 以降(基本 API バージョン 1。拡張は必須ではありません)。
- TaikoNauts 2026.10.01.1 と ModLoader v1.3.1 で動作を確認しています。fever の判定はコードのパターンで見つけるため、ゲームの更新後に見つからなくなる可能性があります。
- NULM アニメーションのパックは、各自で用意してください。**NULM のデータやテクスチャは同梱していません。** 権利は各権利者に帰属します。

## インストール

1. TaikoNauts を終了します。
2. リリースの ZIP を `TaikoNauts\mods` に展開します。`mods\nulm-background` ができます。
3. 使っているスキンの **`Lumens` フォルダ**に、NULM パックを置きます。たとえば `TaikoNauts\Skins\K-Style\Lumens` です。パックごとに 1 つのフォルダを作ります。

   ```text
   Skins\K-Style\Lumens\bg_nomal_a_01\bg_nomal_a_01.nulm
   Skins\K-Style\Lumens\bg_nomal_a_01\bg_nomal_a_01_0.png      (アトラス 0)
   Skins\K-Style\Lumens\bg_nomal_a_01\bg_nomal_a_01_1.png      (アトラス 1。使う場合のみ)
   ```

   フォルダ名、`.nulm` のファイル名、アトラス画像の接頭辞は、同じ名前にします。アトラスは `_0` から `_15` まで、存在するものを読み込みます。
4. ゲームを起動します。読み込まれたレイヤーは `modloader.log` に記録されます。

パックはスキンの中に置くので、スキンごとに別のセットを持てます。スキンを切り替えると、背景も切り替わります。

## パックの選ばれ方

曲が始まるとき、選択中のスキンの `Lumens` フォルダを調べ、レイヤーごとにパックを 1 つランダムに選びます。レイヤーはパック名で決まります。

| 名前の先頭 | レイヤー |
| --- | --- |
| `donbg_` | 上背景 |
| `bg_nomal_`(または `bg_normal_`) | 下背景 |
| `bg_fever_` | fever 背景(ゲージがクリアの間に表示) |
| `bg_dai_` | 下背景の上に重ねる台 |

スキンにそのレイヤーのパックが複数あれば、曲ごとに別のパックが選ばれます。スキンにパックがないレイヤーは、`config.json` で指定しない限り、ゲーム本来の背景のままです。パックは曲の読み込み中に読み込まれ、演奏中には読み込みません。

## 設定(任意)

`config.json` は任意です。スキンの `Lumens` に無いレイヤーに使う固定のパックと、その他の設定を書きます。ここで指定したパックは `mods\nulm-background\packs\<パック名>\` に置きます。`config.example.json` を `mods\nulm-background\config.json` にコピーして編集します。

```json
{
  "upper": { "pack": "donbg_a_01_1p", "x": 0, "y": 0 },
  "lower": { "pack": "bg_nomal_a_01", "x": 0, "y": 540 },
  "fever": { "pack": "bg_fever_a_01", "x": 0, "y": 540 },
  "dai": { "pack": "bg_dai_a_01", "x": 0, "y": 540 },
  "clearDetection": "game"
}
```

| キー | 意味 |
| --- | --- |
| `upper` | 上背景。`x`、`y` の位置に描きます(初期値は `0`、`0`)。 |
| `lower` | 下背景(`y` の初期値は `540`)。`fever` と `dai` を使うには必要です。 |
| `fever` | fever 背景。`lower` の上に描き、ゲージがクリアになると表示します。 |
| `dai` | `lower` と `fever` の上に重ねる台。 |
| `pack` | `packs` の下のフォルダ名。`root` を付けるとルートトラックを指定できます。省略すると、唯一のルートトラックを使います。 |
| `clearDetection` | fever への切り替えの判定方法。`game`(初期値)、`gauge`、`off`。下記を参照してください。 |
| `gauge` | `clearDetection` が `gauge` のときだけ使います。描画されるゲージの `tileWidth`、`tileHeight`、`clearTiles`。 |

どのレイヤーも省略できます。`lower` を省くと、ゲーム本来の下背景がそのまま使われます。座標はゲームの 1920x1080 のレイアウトです。

パックの名前はアーケードのデータに合わせています。`donbg_a_NN_1p`(上)、`bg_nomal_a_NN`(下)、`bg_fever_a_NN`(fever)、`bg_dai_a_NN`(台)です。上背景と fever のタイムラインには、ラベル `init`、`normal_fever`、`fever_normal` が必要です。

## 仕組み

TaikoNauts は演奏画面を 1920x1080 のオフスクリーンのテクスチャに描き、スキンの各背景を「幅 1920 ピクセルの 1 枚のテクスチャ」として描きます。高さは、下背景が約 540、上背景が約 276 です。この MOD は `DrawTexturePro` を監視して、その描画を飛ばし、同じタイミングで NULM を再生します。再生は曲ごとに最初からやり直します。

## fever の判定

fever 背景は、ゲーム自身のクリア判定に従います。そのためスキンに依存しません。TaikoNauts は魂ゲージについて `Gauge.IsGaugeClear` を呼びます。この MOD はその関数を、固定のアドレスではなく機械語のパターンで探し、呼び出しを包んで、ゲームが計算した結果を記録します。MOD の側からゲームのコードを呼ぶことはありません。関数がちょうど 1 か所で見つからない場合(ゲームの更新後に起こりえます)は、何も書き換えず、ログにその旨を出して、fever 背景は使いません。

`clearDetection` は次の値にできます。

- `game`: 上で説明した、初期値の方法です。
- `gauge`: 描画されている魂ゲージから推測します。ゲージは埋まった数だけタイルが並びます。`gauge.clearTiles` 枚(初期値 40、鬼のクリア位置)に達すると fever 背景を表示します。スキンのゲージの絵に依存するので、代替手段です。
- `off`: fever に切り替えません。

ゲームに合わせて動くので、ゲージがクリアの境界を行き来するたびに、背景も切り替わります。これはゲーム本来のクリア状態と同じ挙動です。

## 制限

- NULM のクリップマスク(`ClipDepth`)とテキストフィールドは再現していません。
- 1P のレイアウトのみ対応です。
- 背景を別のサイズで描くスキンは検出できません。

## NULM の中身を調べる

```powershell
build\inspect.exe packs\bg_nomal_a_01\bg_nomal_a_01.nulm
```

フレームレート、サイズ、アトラスの数、ルートトラックとそのラベルを表示します。

## ビルド

MinGW-w64 の GCC と PowerShell が必要です。

```powershell
.\scripts\build.ps1
```

`mods\nulm-background`(設定とパック)を持つゲームフォルダがあれば、`-OriginalRaylib <raylib_original.dll のパス>` を付けると、本物の raylib を通して 1 フレームを画面外に描き、`build\render_smoke.png` に書き出します。スクリプトは `dist\TaikoNauts-NULM-Background-v1.0.0.zip` を出力します。

## ライセンス

MIT。[LICENSE](LICENSE) を参照してください。
