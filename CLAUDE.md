# VECTOR_REGINE

DirectX 12 の自作エンジンと、その上で動くゲーム・エディター。
このファイルは**地図**。ルールや設定の中身はここに書かず、書いてある場所だけを示す。
中身が変わったときは参照先を直し、ここは場所が変わったときだけ直す。

## フォルダの地図

```text
VECTOR_REGINE/
├─ CLAUDE.md              この地図
├─ README.txt             フォルダ名 Common / Core / Internal の意味
├─ .gitignore             git 管理の対象(Asset と Library の扱いもここ)
├─ Library/               外部ライブラリ(git 管理外)
│  ├─ Include/            ヘッダー(Jolt / DirectXTK12 / DirectXTex / imgui-docking / nlohmannJSON ほか)
│  ├─ Lib/                Debug / Release の .lib と .pdb
│  └─ Source/             自前ビルドするライブラリのソースと手順(README.md)
└─ BaseProject/           Visual Studio のプロジェクト一式
   ├─ BaseProject.sln     ソリューション
   ├─ BaseProject.vcxproj ビルド設定(.filters はフィルター)
   ├─ .editorconfig       インデント・改行・文字コード
   ├─ .claude/            Claude Code のローカル設定
   ├─ Source/             C++ ソース(層ごとに分かれる)
   │  ├─ main.cpp / Pch.h
   │  ├─ Core/            どの層にも依存しない道具箱(Core.h)
   │  ├─ Engine/          エンジン本体(MainEngine / EngineCommon.h)
   │  ├─ Application/     ゲーム(App / AppCommon.h)。ECS の Components / Systems もここ
   │  └─ Editor/          エディター(Editor / EditorCommon.h)
   ├─ Asset/              実行時に読むデータ(Shader と Data 以外は git 管理外)
   │  ├─ Shader/          HLSL(Common / Source)
   │  ├─ Data/            起動時に読む設定(Engine / Game / User)
   │  ├─ Scenes/          シーン(<名前>/<名前>.ojscene と .obscene)
   │  ├─ RenderingPipeline/ レンダリングパイプライン(.ojrpipe)
   │  └─ ほか             Model / Texture / Material / Effect / Prefab / ObjectPrefab(GameObject の部分木) / UIPreset(飾りの見た目) / StateMachine など
   ├─ 環境/               設計資料(下の表を参照)
   └─ x64/                ビルド出力(BaseProject.exe)
```

## ルールの書いてある場所

| 知りたいこと | 場所 |
|---|---|
| コーディング規約(命名・namespace・所有・Include・層の依存・コメント・クラスの並び・ECS・RenderGraph) | [BaseProject/環境/Docs/CodingStandards.md](BaseProject/環境/Docs/CodingStandards.md) |
| 規約への適合状況と未修正項目 | [BaseProject/環境/Docs/CodingRulesAudit.md](BaseProject/環境/Docs/CodingRulesAudit.md) |
| UI の作り方(クラスの切り方・親子で伝わるもの・別の UI にする条件) | [BaseProject/環境/Docs/UIGuidelines.md](BaseProject/環境/Docs/UIGuidelines.md) |
| ディレクトリの分け方 | CodingStandards.md の「9. Directory Structure」 |
| 層(Core / Engine / Application / Editor)の依存の向き | CodingStandards.md の「4. Dependency Rules」 |
| フォルダ名 Common / Core / Internal の意味 | [README.txt](README.txt) |
| インデント・改行コード・文字コード | [BaseProject/.editorconfig](BaseProject/.editorconfig) |
| クラスの依存図(Excalidraw)と作り直し方 | [BaseProject/環境/DependenceView/_README.md](BaseProject/環境/DependenceView/_README.md)(生成スクリプトは `環境/gen_excalidraw.py`) |
| エンジン全体のリファクタリングの手順と進み具合 | [BaseProject/環境/REFACTORING_PLAN.md](BaseProject/環境/REFACTORING_PLAN.md) |
| エフェクトのリファクタリング計画 / 保留タスク | [BaseProject/環境/EFFECT_REFACTORING_PLAN.md](BaseProject/環境/EFFECT_REFACTORING_PLAN.md) / [BaseProject/環境/EFFECT_DEFERRED_TASKS.md](BaseProject/環境/EFFECT_DEFERRED_TASKS.md) |
| 外部ライブラリの版とビルド手順 | [Library/Source/README.md](Library/Source/README.md) |
| git 管理するもの・しないもの | [.gitignore](.gitignore) |

## 起動構成の書いてある場所

| 知りたいこと | 場所 |
|---|---|
| ビルド構成(Debug / Release)・Include / Lib パス・プリプロセッサ定義 | [BaseProject/BaseProject.vcxproj](BaseProject/BaseProject.vcxproj) |
| PCH の分け方と .cpp を足したときの設定 | CodingStandards.md の「5. PCH」 |
| ビルドモード(Debug / Development / Shipping)の意味 | CodingStandards.md の「6. Build Configuration」 |
| エンジン設定(ビルドモード・Asset のルート・スレッド数・各オプション)の値 | [BaseProject/Asset/Data/Engine/EngineData.ojoptn](BaseProject/Asset/Data/Engine/EngineData.ojoptn)。項目の定義は `BaseProject/Source/Engine/Option/` 以下 |
| 最初に開くシーン | [BaseProject/Asset/Data/Game/GameData.ojgmdt](BaseProject/Asset/Data/Game/GameData.ojgmdt)(シーンの GUID)。読むのは `Source/Application/Game/GameManager/` |
| ユーザー設定(キー割り当てなど) | [BaseProject/Asset/Data/User/UserData.ojdata](BaseProject/Asset/Data/User/UserData.ojdata)。定義は `Source/Application/Game/UserData/` |
| シーンファイルの置き場と .ojscene / .obscene の使い分け | `BaseProject/Source/Engine/Scene/SceneManager/SceneManager.cpp` のコメント |
| 作業ディレクトリ | `BaseProject/`(Asset のパスがここからの相対のため) |

## 報告の言語

- 作業報告・返答・質問・途中経過のひとことは、すべて日本語で書く(表や見出しも含む)。コード中の識別子やパスは原文のまま。

## 補足

- Asset 配下は Shader と Data 以外 git 管理外。シーンやパイプラインを書き換える前はバックアップを取る。
- `.oj*` は JSON、`.ob*` はバイナリ。開発中は JSON 側を編集する。
- `BaseProject.vcxproj.filters` は改行コードが混在している。
