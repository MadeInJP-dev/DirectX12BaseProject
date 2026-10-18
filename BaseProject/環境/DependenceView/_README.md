# 依存関係図

BaseProject のクラス依存を Excalidraw 形式で書き出したもの。
`.excalidraw` を [excalidraw.com](https://excalidraw.com) にドラッグ&ドロップすると開ける。
同じ名前の `.svg` は同じ配置の確認用で、ブラウザや VS でそのまま開ける。

- このフォルダの図は **2026-10-09 時点** のコード(シングルトンを MainEngine / ResourceManager の2つまで減らした後)。
- それより前の図(2026-09-10 / 09-15)は git の履歴に残っている(旧 `環境/*.excalidraw` と `環境/Before/`)。

## 図の一覧

| ファイル | 中身 |
|---|---|
| `01_Overview.excalidraw` | WinMain → Application / MainEngine / MainEditor の持ち主の木と、残った2つのシングルトン |
| `02_Graphics.excalidraw` | GraphicsEngine の持ち物 → CameraPipelineManager → GraphicsPipeline → RenderGraph → Pass、PassContext の配り方 |
| `03_Scene_ECS_GameObject.excalidraw` | SceneManager → BaseScene → World / GameObjectManager、EngineServices とコンテキストの配り方 |
| `04_Resource.excalidraw` | ResourceManager(MainEngine 所有)/ AssetDatabase / ResourceBuildContext と各アセットの IO |
| `05_Editor.excalidraw` | MainEditor(WinMain 所有)→ ImGuiContext / PanelManager / Profiler と各パネル、IDevTool 越しの呼び出し |
| `06_D3D12_Raytracing.excalidraw` | RenderDevice / DescriptorHeapManager ほか D3D12 まわりと RayEngine 配下 |
| `07_Input_Audio_Option.excalidraw` | InputManager / AudioManager / OptionManager / Particle / JobSystem |

図から出した改善点を、どの順番で直すかは [../REFACTORING_PLAN.md](../REFACTORING_PLAN.md) にまとめてある(進み具合もそこに書いている)。

## 表記

| 見た目 | 意味 |
|---|---|
| 緑の枠 | シングルトン(`static X& Instance()` を持つクラス) |
| 白の枠 | 通常のクラス |
| 白の破線枠 | 引数で配るコンテキスト構造体(EngineServices / SystemContext / ObjectContext / CompEditContext / PassContext / ResourceBuildContext / EditorContext) |
| 緑の矢印 | シングルトン経由の依存。`X::Instance()` を直接呼んでいる |
| 白の実線矢印 | 所有(`unique_ptr` / 値メンバ) |
| 白の破線矢印 | 参照(引数・ポインタで受け取る) |
| 白の点線矢印 | 継承 |
| 橙の枠(右下) | 改善点。そのシートで気になった依存と、直し方の当たり |

色は Excalidraw の**ライトモードの値**(黒/緑)で保存している。
Excalidraw のダークモードは描画時に色を反転するので、ダークテーマで開くと
「黒背景・白枠・緑枠」に見える。

## シングルトン(13個 → 10個 → 2個)

残っているのは `MainEngine` と `ResourceManager` だけ。

| クラス | 残した理由 |
|---|---|
| `MainEngine` | アプリの入口そのもの。アプリに1つで、寿命もアプリと同じ |
| `ResourceManager` | 実体は `MainEngine` の `unique_ptr`。`Instance()` は `ResourceRef<T>` のための入口として残した。`ResourceRef<T>` は値として資産に埋まり、コピー・破棄のたびに参照カウントを触るので引数で渡せない |

外れたもの:

| クラス | 外した日 | 今の持ち主 | 使う側への配り方 |
|---|---|---|---|
| `D3D12Wrapper` | 09-15 | 削除。`GraphicsDevice` / `CommandContext` / `FrameManager` などに分けた | `RenderDevice`(`GraphicsEngine` 所有) |
| `DescriptorHeapManager` | 09-15 | `GraphicsEngine` | `Init` の引数・各コンテキスト |
| `AssetDatabase` | 09-15 | `ResourceManager` | `EngineServices` / `ResourceBuildContext` |
| `RayEngine` | 09-15 以降 | `GraphicsEngine` | `EngineServices` / `PassContext` |
| `ObjectMetaRegistry` | 10-09 | `MainEngine` | `EngineServices::pObjectRegistry` |
| `MainEditor` | 10-09 | `WinMain`(main.cpp) | エンジンへは `IDevTool`、パネルへは `EditorContext`(`pEffectEditor` / `pEditorCamera`) |
| `InputManager` | 10-09 | `MainEngine` | `EngineServices`。ウィンドウのフォーカスは `NativeWindow::SetFocusCallback` でつなぐ |
| `AudioManager` | 10-09 | `MainEngine` | `EngineServices`。サウンドインスタンスは発行元を持ち、読み込みは `ResourceBuildContext::pAudioEngine` |
| `OptionManager` | 10-09 | `MainEngine` | `EngineServices`。受け手へは `MainEngine` が流し込む(InputOption は `Init`、DebugDraw は毎フレーム。音量はシーンの `SceneAudio` が持つ) |
| `GameManager` | 10-09 | `Application` | シーンをまたぐ記録はワールドの `GameDataResource` から引く |
| `SceneManager` | 10-09 | `MainEngine` | `EngineServices::pSceneManager`。プレハブの読み込みは `ResourceBuildContext::pSceneManager` |

## 数値

`Instance()` / `GetInstance()` の呼び出し(下のコマンドの件数。コメント内のものを含む)。

| シングルトン | 2026-09-10 | 2026-09-15 | 2026-10-09 作業前 | 2026-10-09 |
|---|---:|---:|---:|---:|
| ResourceManager | 158 | 12 | 12 | 12(すべて `ResourceRef<T>` の中) |
| AssetDatabase | 95 | 0 | 0 | 0 |
| D3D12Wrapper | 68 | 0 | 0 | 0 |
| MainEditor | 64 | 17 | 4 | 0 |
| MainEngine | 62 | 64 | 81 | 65 |
| DescriptorHeapManager | 62 | 0 | 0 | 0 |
| SceneManager | 35 | 35 | 42 | 0 |
| OptionManager | 30 | 28 | 22 | 0 |
| AudioManager | 15 | 15 | 12 | 0 |
| InputManager | 13 | 13 | 11 | 0 |
| GameManager | 12 | 12 | 12 | 0 |
| RayEngine | 11 | 5 | 0 | 0 |
| ObjectMetaRegistry | 4 | 4 | 4 | 0 |
| **合計** | **629 回 / 142 ファイル** | **205 回 / 62 ファイル** | **200 回 / 68 ファイル** | **77 回 / 27 ファイル** |

`MainEngine` の 65 回の内訳 : App.cpp 13 / エディター 29 / InputManager 6 / 描画層 11(BLAS・パーティクル・RenderGraph::MakeContext・デルタタイムを引く2つのパス)/ その他 6(main.cpp・Archive・Mesh・ScopedResourceBuild・InputActionManager)。

層をまたぐ向き:

| 向き | 2026-09-10 | 2026-09-15 | 2026-10-09 | |
|---|---:|---:|---:|---|
| Engine → Editor | 38 | 11 | 0 | MainEngine / SceneManager は `IDevTool` だけを見る |
| App → Editor | 21 | 4 | 0 | Application / GameManager / InputActionManager も `IDevTool` 越し |
| Engine → Game | 1 | 1 | 0 | GraphicsEngine → GameManager は解消済み |
| 循環(シングルトン経由) | 1組 | 0組(D3D12層) | 0組 | Option から Instance() で押し込む形は解消。型の上では WindowOption → MainEngine と InputOption ⇄ InputManager が残る(エディターで値を変えた瞬間に反映するため) |

## 作り直し方

`環境` フォルダで実行する。

```
python gen_excalidraw.py
```

`.excalidraw` と `.svg` がこのフォルダ(`環境/DependenceView/`)に出る。
Windows のコンソールで文字化けする場合は `PYTHONUTF8=1` を付ける。
ノードと矢印の定義はスクリプト下部の `s1` 〜 `s7` にべた書きしてあるので、
クラスを足したらそこへ追記する。
右下の改善点は同じくスクリプト下部の `sX.imp(...)` にまとめてある。

緑矢印の元データは次のコマンドで洗い出せる(`Source` フォルダで実行)。

```
rg -o "(MainEngine|SceneManager|ResourceManager|AssetDatabase|InputManager|AudioManager|MainEditor|D3D12Wrapper|DescriptorHeapManager|RayEngine|GameManager|ObjectMetaRegistry|OptionManager)::(Instance|GetInstance)\(\)" .
```

ファイルごと・クラスごとに数えるなら:

```
rg -o "(MainEngine|SceneManager|ResourceManager|AssetDatabase|InputManager|AudioManager|MainEditor|D3D12Wrapper|DescriptorHeapManager|RayEngine|GameManager|ObjectMetaRegistry|OptionManager)::(Instance|GetInstance)\(\)" . | sed -E 's/::(Instance|GetInstance)\(\)//' | sort | uniq -c
```

## メモ

- `EngineServices` は `MainEngine` が正本を持ち、`World` が写しを持つ。中身は MainEngine / ResourceManager / AssetDatabase / InputManager / RayEngine / AudioManager / JobSystem / PhysicsEngine / OptionManager / DebugDraw / ObjectMetaRegistry / SceneManager の12個。
- `MainEngine` の持ち物は宣言順に ResourceManager → OptionManager → InputManager → AudioManager → ComponentMetaRegistry → ObjectMetaRegistry → SceneManager → ウィンドウ以下。メンバは宣言の逆順に壊れるので、ResourceRef を持つもの(オーディオ・シーン)は ResourceManager より先に壊れる。
- シーンの解放(`SceneManager::Release`)だけは、ゲームの解放より前に `Application` が呼ぶ。シーンのオブジェクトがゲームの記録(`GameDataResource` → `GlobalGameContext`)を指しているため。
- レンダーパスは 37 種類。`Instance()` を呼ぶのはデルタタイムを引く `GroundFieldPass` / `SceneVolumetricFogPass` の2つ。`PassContext` を組むのは `RenderGraph::MakeContext` だけ。
