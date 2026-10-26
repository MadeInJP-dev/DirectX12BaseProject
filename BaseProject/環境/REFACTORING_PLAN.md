# リファクタリング手順

依存関係図（`01_Overview` 〜 `07_Input_Audio_Option`）で見えた汚さを、
どの順番で直していくかの計画と、その進み具合。

- 今の図 … `DependenceView/`（2026-10-09。作り直しは `python gen_excalidraw.py`）
- 計画を立てた時点（2026-09-10）と 2026-09-15 の図 … git の履歴（旧 `環境/Before/` と `環境/*.excalidraw`）

---

## 0. 先にゴールを決める

**シングルトンをゼロにするのは目的ではない。**

「アプリに1つしか無く、生存期間がアプリと同じ」ものは、
シングルトンのままでも設計として説明がつく。
面接で強いのは「全部消しました」ではなく「ここは残す判断をしました、理由は〜」の方。

消すべきものは3つに絞る。

| # | 直すもの | なぜ |
|---|---|---|
| A | **層の逆流** | エンジンがエディターを見る / 描画層がゲームを見る。依存の向きが説明できなくなる |
| B | **循環** | `D3D12Wrapper` ⇄ `DescriptorHeapManager` など。初期化順と解放順が暗黙になる |
| C | **どこからでも書ける状態** | 「誰がこれを触るのか」がコードから読めない。バグの原因が追えない |

逆に、**残してよいもの**は最初に決めておく（後で迷わないため）。

- `D3D12Wrapper` / `DescriptorHeapManager` … デバイスとディスクリプタヒープはプロセスに1つ
- `MainEngine` … アプリの入口そのもの
- `MainEditor` … ツール層。ただし**エンジン側から見られる**のは無し（＝A）

> **実際にやってみての変更（2026-09-15）**
> `D3D12Wrapper` / `DescriptorHeapManager` は「残してよい」側に置いていたが、
> フェーズ2で Device を引数にした結果、持ち主（`GraphicsEngine`）に入れる方が素直だと分かり、両方ともシングルトンを外した。
> 「残す判断」の例としては、代わりに `ResourceManager::Instance()` を残している
> （`ResourceRef<T>` が値として資産に埋まり、引数で渡せないため）。
>
> **さらに進めた結果（2026-10-09）**
> 残っていた `ObjectMetaRegistry` / `MainEditor` / `InputManager` / `AudioManager` / `OptionManager` / `GameManager` / `SceneManager` も
> 持ち主の `unique_ptr` にした（フェーズ7）。`MainEditor` は「残してよい」側に置いていたが、
> エンジンからは `IDevTool` としてしか見えていなかったので、`WinMain` が持つ形にした方が説明が素直になった。
> 残したシングルトンは `MainEngine`（アプリの入口）と `ResourceManager::Instance()`（`ResourceRef<T>` 用の入口）の2つだけ。

---

## 1. ベースラインと現在

`Instance()` / `GetInstance()` の呼び出し。

| | 2026-09-10 | 2026-09-15 | 2026-10-09 |
|---|---:|---:|---:|
| 呼び出し回数 | 629 | 205 | **77** |
| ファイル数 | 142 | 62 | **27** |
| シングルトンの数 | 13 | 10 | **2** |

| シングルトン | 2026-09-10 | 2026-09-15 | 2026-10-09 | メモ |
|---|---:|---:|---:|---|
| ResourceManager | 158 | 12 | 12 | 残りは全部 `ResourceRef<T>` の中。実体は MainEngine 所有 |
| AssetDatabase | 95 | 0 | 0 | ResourceManager の持ち物になった |
| D3D12Wrapper | 68 | 0 | 0 | 削除 |
| MainEditor | 64 | 17 | 0 | WinMain の持ち物。エンジンへは IDevTool として差し込む |
| MainEngine | 62 | 64 | 65 | 10-09 の作業前は 81。持ち主へ渡す経路で一部減った |
| DescriptorHeapManager | 62 | 0 | 0 | GraphicsEngine の持ち物になった |
| SceneManager | 35 | 35 | 0 | MainEngine の持ち物。EngineServices で配る |
| OptionManager | 30 | 28 | 0 | MainEngine の持ち物。受け手へは MainEngine が流し込む |
| AudioManager | 15 | 15 | 0 | MainEngine の持ち物 |
| InputManager | 13 | 13 | 0 | MainEngine の持ち物 |
| GameManager | 12 | 12 | 0 | Application の持ち物。記録はワールドの GameDataResource から引く |
| RayEngine | 11 | 5 | 0 | GraphicsEngine の持ち物 |
| ObjectMetaRegistry | 4 | 4 | 0 | MainEngine の持ち物 |

層をまたぐ向き：

```
                 2026-09-10   2026-09-15
Engine -> Editor     38           11     ← A(逆流)  MainEngine の駆動 8 / SceneManager の通知 3
App    -> Editor     21            4     ← A(逆流)  App.cpp 2 / RegisterEditFunc 2
Engine -> Game        1            1     ← A(逆流)  GraphicsEngine -> GameManager::Draw()
循環(D3D12層)        1組          0組    ← B
```

---

## 2. 進め方の原則

1. **1フェーズ = 1ブランチ = 1テーマ。** 途中で別のことを直したくなっても、メモに残して手は出さない。
2. **フェーズの終わりで必ず起動確認。** 起動 → シーン読み込み → ゲームモード往復 → エディター操作。自動テストが無いので、この手順書だけは最初に作る。
3. **ビルドが何日も通らない状態を作らない。** 特にフェーズ4は、アセット種別ごとに区切る。
4. **数を測りながら進める。** 各フェーズの前後で下のコマンドを叩き、README に記録する。

```bash
rg -o "(MainEngine|SceneManager|ResourceManager|AssetDatabase|InputManager|AudioManager|MainEditor|D3D12Wrapper|DescriptorHeapManager|RayEngine|GameManager|ObjectMetaRegistry|OptionManager)::(Instance|GetInstance)\(\)" Source | wc -l
```

---

## 3. 順番と進み具合

| 順 | フェーズ | 直すもの | 効果（目安） | 状態 |
|---|---|---|---|---|
| 1 | 横断的関心事を切る | A | −40回・逆流59本のほぼ全部 | **完了** |
| 2 | 循環を切る | B | −20〜30回・循環1組 | **完了**（計画より踏み込んで D3D12Wrapper を削除） |
| 3 | PassContext を太らせる | C | −30〜60回 | **ほぼ完了**（描画層→シーン/ゲームの逆流が残る） |
| 4 | ResourceBuildContext の徹底 | C | −100〜150回 | **ほぼ完了**（Sound / Mesh / Prefab が残る） |
| 5 | 所有関係の整理 | C | −30回 | **ほぼ完了**（残りは MainEngine の付け替え組。フェーズ7 で大半が片付いた） |
| 6 | 図とドキュメントの作り直し | — | 見せ物 | **済み**（2026-09-15、2026-10-09 に再生成） |
| 7 | 残りのシングルトンを外す | C | −120回 | **完了**（2026-10-09。シングルトン 9 → 2） |

**なぜこの順番か。**（計画時の考え。結果的にこの順で問題なかった）

- フェーズ1は「呼び出しの置き換えだけ」でロジックが1行も動かない。効果は最大級（逆流がほぼ全部消える）なのに危険が最小。**最初にやると図が目に見えて変わるので、続ける気力が持つ。**
- フェーズ4は呼び出し数では最大だが、**そこから始めてはいけない。** 機械的な置換が何日も続き、その間ビルドが不安定で、しかも図の見た目は「緑矢印が減る」だけ。仕組み（Context の形）が固まってから流し込む。
- フェーズ3を4より先にするのは、レンダーグラフがこのプロジェクトの見せ場だから。時間切れになったとき、手が入っているのがそこである方がよい。

---

## 4. 各フェーズの中身

### フェーズ1 — 横断的関心事（ログ・計測・デバッグ描画）を切る　【完了】

**計画**

`Engine::Debug::SetLogCallback` があるのに、14箇所が `MainEditor::Instance().AddLog / ErrorLog` を直接呼んでいた。
**自分で作った仕組みを使い切れていない**だけなので、置き換えるだけで終わる。同じ形を計測とデバッグ描画にも広げる。

| 用途 | 前 | 後 |
|---|---|---|
| ログ | `MainEditor::Instance().AddLog / ErrorLog` | `ENGINE_LOG` / `ENGINE_WARNING` / `ENGINE_ERROR` / `ENGINE_ERRLOG` |
| 計測 | `MainEditor::Instance().StartTimer / StopTimer` | `ENGINE_PROFILE_SCOPE`（RAII。結果は `SetProfileCallback` で Profiler へ） |
| デバッグ描画 | `MainEditor::Instance().DrawBox / GetDebugLineDataVec` | `Engine::Graphics::DebugDraw`（GraphicsEngine 所有。EngineServices で配る） |

**結果**

- `MainEditor` の `AddLog` / `StartTimer` / `Draw*` 系は削除。`MainEditor::Init` がログと計測のコールバックを登録し、`Release` で外す。
- `EngineServices` から `MainEditor` を外し、代わりに `DebugDraw` を載せた。表示のオンオフは `DebugDrawOption`。
- `Engine -> Editor` 38 → 11、`App -> Editor` 21 → 4。
  ECS（World / ComponentMetaRegistry）・CollisionWorld・BVHTraverser・Archive・PipelineState・AssetDatabase からエディターへの依存は 0。
- 残り（エディターを「駆動」「通知」しているもの）は **フェーズ5の残り** に回した。

> 面接で使える言い方：「ログ・計測・デバッグ描画は層をまたぐので、
> インターフェースと登録の形にしてエンジンからツールへの依存を切りました」

### フェーズ2 — 循環を切る　【完了】

**計画**

`DescriptorHeapManager` が `D3D12Wrapper` を引いているのは **Device が欲しいだけ**。
`DescriptorHeapManager::Init(Device*)` にすれば循環が切れる。

**結果（計画より踏み込んだ）**

- `DescriptorHeapManager` は Device を `Init` で受け取り、`GraphicsEngine` が `unique_ptr` で持つ形にした。
- Device を引数で配れるようになったら `D3D12Wrapper` に残る役目が無くなったので、
  `GraphicsDevice` / `BackBuffer` / `CommandContext`（+ `CommandPool`）/ `FrameManager` / `AsyncGPUManager` に分けて **`GraphicsEngine` へ集約し、`D3D12Wrapper` を削除**した。
- D3D12 層（GPUResource / Texture / バッファ群 / RootSignatureBuilder / CBAllocator）は `Device*` と `DescriptorHeapManager*` を引数で受け取り、上の層を見ない。
- `D3D12Wrapper` 68回 + `DescriptorHeapManager` 62回 → 0。図06の相互矢印が消えた。

**気付いたこと**

- シングルトンを外した分の一部は `MainEngine::Instance().RefGraphicsEngine()` に付け替わっただけで、
  `MainEngine` の呼び出しは 62 → 64 と減っていない（BLAS / Mesh / ScopedResourceBuild / ImGuiContext / EditorHelper / SceneViewPanel）。
  **置き場が MainEngine に移っただけ**の箇所は、フェーズ5で引数に直す。

### フェーズ3 — PassContext を太らせる　【ほぼ完了】

**計画**

30種あるパスが `ResourceManager` / `AssetDatabase` / `RayEngine` / `MainEngine` を個別に引いていた。
`PassContext` に足して、組み立て位置で1回だけ詰める。

**結果**

- `PassContext` に `pMainEngine` / `pResourceManager` / `pAssetDatabase` / `pRayEngine` / `pParticleManager` / `pHeapManager` を追加。
- 組むのは `RenderGraph::MakeContext` だけ。**パス（30種類）の `Instance()` は 0。**
- `RenderContext` / `ParticleBufferManager` / `MouseCursor` / `RayEngine::CommitWorld` もヒープ・リソースマネージャーを引数で受け取る形になった。

**残っていること**

- `RenderGraph::MakeContext` 自身が `MainEngine` と `RayEngine` を `Instance()` で引いて詰めている。
  GraphicsEngine から受け取れば、描画層でシングルトンを引くのは GraphicsEngine の入口だけになる。
- **`GraphicsEngine -> SceneManager / GameManager` の逆流が残っている**（計画では同時にやる予定だった）。
  - `GameManager::Draw()`（「テスト」とコメントされた呼び出し）
  - `SceneManager::RefWorld()`（BLAS 初期化キューをワールドのリソースから読む）
  必要なデータはシーン更新側から積む形に変える。
- `ParticleSimulation` / `GPUParticlePool` が `MainEngine` を引く（パーティクルマネージャー・デルタタイム・遅延解放）。
- `GraphicsEngine` / `MouseCursor` / `DebugDraw` が `OptionManager` を直接読む。

### フェーズ4 — ResourceBuildContext の徹底（本丸）　【ほぼ完了】

**計画**

`ResourceManager` 158 + `AssetDatabase` 95 = 253回、全体の40%。種別ごとに区切って Context へ流し込む。

**結果**

- `ResourceManager` 158 → 12、`AssetDatabase` 95 → 0。IO 群と Model / Texture / Material / Shader / Animator / ActionStateMachine / Particles / EffectAsset / AudioBehavior / Font / RenderingPipelineAsset の直引きは 0。
- `ResourceBuildContext` に `pDevice` / `pHeapManager` / コマンドリスト3種 / `pGraphicsEngine` / `pMeshBufferAllocator` / `pPassMetaRegistry` / `pResourceManager` / `pAssetDatabase` が揃った。
- 計画で「最後に手を付ける」としていた **`ResourceManager.h` の自己 `Instance()` 12箇所**は、
  すべて `ResourceRef<T>` のコピー・破棄の中だった。値として資産に埋まるので引数で渡せない、という理由で **残す判断**をした。
  それに合わせて `Instance()` は「MainEngine が作った実体を指す入口」に変え、自分では作らないようにした。
- `ShadingModelTable` はシェーディングモデルごと削除したので対象から外れた。

**残っていること**

- `Sound` / `SoundIO` → `AudioManager`（読み込みにオーディオエンジンが要る）。Context に載せる。
- `ScopedResourceBuild` → `MainEngine`（バッチを開くのに GraphicsEngine が要る）。引数で受け取る。
- `Mesh::Release` → `MainEngine`（MeshBufferAllocator へ返す。Release には Context が来ない）。
- `Prefab` → `SceneManager`（生成先のワールドを決める）。引数で World を受け取る。

### フェーズ5 — 所有関係の整理　【途中】

**済んだこと**

- **`ResourceManager` の持ち主を `MainEngine` にし、`AssetDatabase` は `ResourceManager` に持たせた。**
- **`BaseScene` の直引き 9個 → 2個。** 残りはワールドの作成（`SceneManager::CreateWorld`）と `EngineServices` の写し。
- `ParticleBufferManager` / `AudioManager` / `MouseCursor` は持ち物を `Init` の引数で受け取る形になった。
- アプリ側：`GunShootSystem → ResourceManager` と、`ModelComponent` / `ParticlesComponent` のヘッダー内 `Instance()` は解消。
- エディター：`EditorContext` に `pServices` / `pProfiler` / `pEditorCamera` を載せ、パネルの多くはそこから引くようになった。

**残っていること（次にやる順）**

> 2026-10-09 時点 : 1・2・3・5・6・7・8 は済み（1・2 はフェーズ7 より前に `IDevTool` で、3・5〜8 はフェーズ7 で解消）。
> 残っているのは 4 の付け替え組だけ（`EditorHelper` / `SceneViewPanel` などのエディター側と、BLAS / Mesh / ScopedResourceBuild / InputManager / パーティクル）。

1. **エディターへの逆流の残り**（A）
   - `SceneManager → MainEditor::OnSceneChanged / RefEffectEditor` … 通知なのでコールバック登録の形にする。
   - `GameManager` / `InputActionManager → MainEditor::RegisterEditFunc`、`App.cpp → EndProfileFrame / IsModalActive`。
   - `MainEngine → MainEditor`（Init / Update / Draw / Release）は入口の駆動として許容。
2. **描画層の逆流**（A、フェーズ3の残り）… `GraphicsEngine → GameManager / SceneManager`。
3. **`OptionManager` の押し込みをやめる**（B）… `WindowOption → MainEngine` /
   `AudioOption → AudioManager` / `InputOption → InputManager` を、受け手が起動時と変更通知で読む形にして片方向にする。
4. **`MainEngine::Instance().RefGraphicsEngine()` の付け替え組**（C）… BLAS / Mesh / ScopedResourceBuild / ImGuiContext / EditorHelper / SceneViewPanel。
   `InputManager → MainEngine`（モードとウィンドウ）、`GPUParticlePool → MainEngine`（遅延解放）も同じ扱い。
5. **アプリ側の直引き**（C）… `Sequence 群 → SceneManager / GameManager` / `ScoreSystem・ScoreHUD → GameManager` /
   `CameraStartSystem → OptionManager`。`EngineServices` に `SceneManager` が無いのが消えない理由なので、載せるか細い口を足すかを先に決める。
6. **コンポーネントのヘッダー内 `Instance()`**（C）… `SoundComponent` / `HitSoundComponent` / `FlyingSoundResource → AudioManager`、
   `FollowTargetComponent` / `AttachmentSlotsComponent → SceneManager`。
7. **`RayEngine` を GraphicsEngine の持ち物にする**（C）… 持ち主が居ないので MainEngine と RenderGraph が引いている。
8. **エディターパネルの残り**（C）… `SceneViewPanel`（17回）と `EffectEditor`（9回）。EditorContext に SceneManager / GraphicsEngine への経路を足す。

**ついでに見つけたもの（コードには手を入れていない）**

- `MainEngine.h` に `m_upRenderContextVec` が宣言だけ残っている（実体は GraphicsEngine 側で使っている）。
- `FrameResourceManager` はどこからも使われておらず、`.cpp` も空。

### フェーズ6 — 図とドキュメントの作り直し　【済み】

- `gen_excalidraw.py` の `s1`〜`s7` を 2026-09-15 のコードに合わせて書き直し、図を再生成した。
- 計画時点の図は `Before/` に残した（同じレイアウトの考え方なので並べて見比べられる）。
- 数値（629 → 205、逆流 38+21 → 11+4、循環1組 → 0組）は README にも載せた。
- フェーズ5が進んだら、もう一度 `python gen_excalidraw.py` で作り直す。
- 2026-10-09 : フェーズ7 の後に `s1`〜`s7` を書き直して再生成した。出力先は `DependenceView/`（`.excalidraw` と `.svg` を同じ場所へ）。

### フェーズ7 — 残りのシングルトンを外す　【完了 2026-10-09】

**方針**

「アプリに1つで寿命も同じ」なら残してよい、という0章の基準は変えない。
ただし持ち主がはっきりしているもの（MainEngine が Init / Release を呼んでいるもの）は、
実体も持ち主の `unique_ptr` に入れた方が寿命と解放順がコードから読める。
外しやすい順（呼び出しの少ない順）に進めた。

| 順 | クラス | 持ち主 | 使う側への経路 |
|---|---|---|---|
| 1 | `ObjectMetaRegistry` | MainEngine | `EngineServices::pObjectRegistry`（GameObjectManager は ObjectContext から） |
| 2 | `MainEditor` | WinMain（main.cpp） | エンジンへは `IDevTool`、パネルへは `EditorContext::pEffectEditor` / `pEditorCamera` |
| 3 | `InputManager` / `AudioManager` / `OptionManager` | MainEngine | `EngineServices`。Option の値は MainEngine が受け手へ流し込む |
| 4 | `GameManager` | Application | シーンをまたぐ記録はワールドの `GameDataResource` から引く |
| 5 | `SceneManager` | MainEngine | `EngineServices::pSceneManager`。プレハブの読み込みは `ResourceBuildContext::pSceneManager` |

**結果**

- シングルトン 9 → 2、`Instance()` 200 回 / 68 ファイル → 77 回 / 27 ファイル。
- Option の押し込み（フェーズ5 の 3）は、受け手が引数で受け取る形にした。
  `InputManager::Init(InputOption*)` / `AudioOption::Apply(AudioManager&)` / `DebugDraw::SetWireEnabled`（MainEngine が毎フレーム）/
  エディターで動かしたときは `IOption::DrawEdit` の `EngineServices` から。
- 2026-10-26 : `OptionManager` を廃止した。設定は持ち主(`NativeWindow` / `TimeManager` / `InputManager` / `SceneView` /
  `DebugDraw` / `MainEngine` / App の `MouseCursor`)が `XxxConfig` で持ち、`ConfigRegistry` は読み書きと設定画面への一覧だけを受け持つ。
  上の「MainEngine が毎フレーム流し込む」形(`SetWireEnabled` / `SetJitterEnabled`)も無くなった。詳細は CodingStandards.md の 6.3。
- `NativeWindow` は `InputManager` を知らなくなった。フォーカスの出入りは MainEngine がつないだ通知先（`SetFocusCallback`）を呼ぶ。
- `SoundInstance` は発行元の `AudioManager` を持つ。`SoundIO` は `ResourceBuildContext::pAudioEngine` から作る
  （ResourceManager が起動時に `SetAudioEngine` で預かり、ローダーのコンテキストへ載せる。`SetJobSystem` と同じ形）。
- Sequence 群はボタンを結ぶとき（`TryBind～`）に `ObjectContext.pServices->pSceneManager` を受け取り、押下時のラムダへ掴ませる。
- `SceneManager` は持ち主（MainEngine）を受け取り、`BaseScene::Enter` / `CreateSceneWorld` へ渡す。
  シーン・ECS・GameObject の中の `MainEngine::Instance()` も 0 になった。
- 解放順 : シーン（Application が先に Release）→ ゲーム → エンジン。MainEngine のメンバは宣言の逆順に壊れるので、
  ResourceRef を持つもの（オーディオ・シーン）は ResourceManager より先に壊れる。

**確かめたこと**

- Debug / Release のビルド（Debug はリビルドして警告 0）。
- Debug で起動 → タイトルまで読み込み → ウィンドウを閉じる、を3回。どれも終了コード 0 で、デバッグ出力にエラーは無い。
  初回の1回だけ起動直後から応答しなくなった（原因は特定できていない。再現せず、変更前のビルドでも同じ手順は通った）。
- ECS の Update フェーズで「依存が循環しています（16 件）」が出ているが、変更前のビルドでも同じなので今回とは別件。

---

## 5. 時間が足りなくなったら

上から順に落とす。

- **フェーズ5の 5〜8 を落とす** … アプリ側・パネル側の直引きは「アプリ寿命のサービスを引いている」だけで、説明がつく。
- **フェーズ5の 3〜4 を落とす** … 循環（Option の押し込み）は残るが、初期化順は README に書いておけば説明できる。
- **フェーズ5の 1〜2（逆流の残り）だけは通したい。** これで「エンジンがエディターとゲームを知らない」と言い切れる。

**逆にやってはいけないこと**：締め切り直前に大きな置換（SceneManager の経路追加など）を始めること。
動いていたものが動かなくなるリスクが、得られる見栄えに見合わない。

---

## 6. 就活での見せ方

リファクタリングそのものより、**判断を説明できること**が効く。
この `環境/` フォルダをそのまま提出物にできる形にしておく。

用意するもの：

1. **before / after の図**（`Before/` と直下。緑矢印が減っているのが一目で分かる）… 済み
2. **数値**（629 → 205、ファイル 142 → 62、シングルトン 13 → 10、Engine→Editor 38 → 11、循環1組 → 0組）… 済み
3. **残したシングルトンとその理由**（0章。`ResourceManager::Instance()` を `ResourceRef<T>` のために残した話が具体例になる）
4. **各フェーズで何を考えたか**
   - フェーズ1の「自分で作った仕組みを使い切れていなかった」は自己批判として素直で、印象がいい
   - フェーズ2で「残す予定だったものを、やってみて外した」判断の変化
   - `MainEngine` の呼び出しが減らなかった理由（置き場が移っただけの箇所がある）を自分で指摘できること

聞かれたときに答えられるようにしておくこと：

- 「なぜシングルトンを使ったのか」→ 当時の理由と、今どう思っているか
- 「なぜ全部消さなかったのか」→ 0章の判断基準と `ResourceRef<T>` の例
- 「どうやって安全に直したのか」→ 段階の切り方、起動確認の手順、測り方
