# Coding Rules

このドキュメントでは、本プロジェクトにおけるコーディング規則、命名規則、設計上の制約、ディレクトリ構成などを定義する。

---

# 1. Naming Convention

## 1.1 基本ルール

* 変数名・関数名は `camelCase` を基本とする。
* 変数の種類に応じて Prefix を付ける。
* Prefix の後ろは `camelCase` とする。
* 定数は基本的に大文字のみを使用し、Prefix は付けない。
* 定数の単語区切りには `_` を使用する。
* `enum` は使用せず、`enum class` のみを使用する。
* `enum class` の型名は `E` から始める。
* 一般的に定着している省略語は使用してよい。
* 頭文字のみを使用した独自の省略は使用しない。
* 構造体のメンバはプレフィックスなし

### 例

```cpp
// OK
_textureManager
_renderContext
_rayTracing
_vertexBuffer
_renderGraph

// 一般的な省略語は許可
_gpu
_cpu
_d3d
_dxgi
```

```cpp
// NG
_texMgr
_rndCtx
_rt
_vtxBuf
_rg
```

ただし、`RT`、`GPU`、`CPU`、`DXGI` など、一般的に広く使用されている技術用語・略語については使用してよい。

### 許可する略語

次の略語は「一般的に定着している省略語」として使用してよい。ここに無い短縮は使わず、単語をそのまま書く。

| 略語 | 元の語 | 例 |
| --- | --- | --- |
| `GPU` / `CPU` / `D3D` / `DXGI` / `RT` | 技術用語 | `_gpuHandle` |
| `ctx` | Context | `a_ctx`, `_pCtx` |
| `cmd` | Command(コマンドリスト・コマンド) | `_cmdList`, `a_cmd` |
| `buf` | Buffer | `_buf` |
| `tex` | Texture | `_tex` |

NG 例の `_rndCtx` / `_vtxBuf` / `_texMgr` は、`ctx` / `buf` / `tex` ではなく `rnd` / `vtx` / `Mgr` が独自の省略なので NG。

---

## 1.2 Prefix

| 種類          | Prefix | 例                |
| ----------- | ------ | ---------------- |
| 引数          | `a_`   | `a_textureDesc`  |
| ローカル変数      | `_`    | `_textureDesc`   |
| メンバ変数(`class`) | `m_`   | `m_textureDesc`  |
| staticメンバ変数 | `s_`   | `s_textureCount` |
| 構造体(`struct`)のフィールド | なし | `maxFuel` |

### 構造体のフィールド

`struct` はデータだけを持つ型(ECS の Component・Desc・定数バッファ・イベントなど)に使い、
公開フィールドには Prefix を付けない。`m_` は振る舞いを持つ `class` のメンバ変数に付ける。

```cpp
struct BoostParamsComponent
{
    float boostPower = 30.0f;   // Prefix なし
};

class Texture
{
private:
    ETextureUsage m_usage = ETextureUsage::None;   // m_
};
```

保存キーとフィールド名を揃えておけるので(`a_ar.Field("boostPower", _comp.boostPower)`)、
データの型はこの形を保つ。振る舞い(関数)や隠したい状態が要るものは `class` にする。

---

## 1.3 Enum

`enum` は使用せず、必ず `enum class` を使用する。

Enum の型名は `E` を Prefix とする。

```cpp
enum class ERenderPhase
{
    Shadow,
    Geometry,
    Lighting
};
```

Enum の値は `PascalCase` を使用する。

---

## 1.4 Namespace

Namespace はディレクトリ構成と対応させる。

`Core`・`Engine`・`App`・`Editor` はトップレベル Namespace とする(ディレクトリは `Source/Core`・`Source/Engine`・`Source/Application`・`Source/Editor`)。

自身が所属するディレクトリの役割を Namespace に反映する。

```cpp
namespace Core::Math
{
}

namespace Engine::Graphics
{
}

namespace Engine::ECS
{
}

namespace App::Game
{
}

namespace Editor::Inspector
{
}
```

Utility 系の Namespace は、所属する機能・役割を示す Namespace を使用する(`Core::String`・`Core::File` など。`Utility` という名前の Namespace は作らない)。

### Engine/Graphics

| ディレクトリ | Namespace |
| --- | --- |
| `Engine/Graphics` 直下・`Device`・`Frame`・`FrameCompute`・`LightManager`・`PipelineState`・`DebugDraw`・`BackBuffer` | `Engine::Graphics` |
| `Engine/Graphics/D3D12` | `Engine::Graphics::D3D12` |
| `Engine/Graphics/Raytracing` | `Engine::Graphics::Raytracing` |
| `Engine/Graphics/Particle` | `Engine::Graphics::Particle` |
| `Engine/Graphics/Animation` | `Engine::Graphics::Animation` |
| `Engine/Graphics/RenderingPipeline` | `Engine::Graphics::Pipeline` |

`Engine::Graphics` の外(`Engine::ECS`・`Engine::Resource`・Editor など)からは `Graphics::D3D12::Device` のように `Graphics` から書く。

### App の ECS

| ディレクトリ | Namespace |
| --- | --- |
| `Application/Components` | `App::Component` |
| `Application/Systems` | `App::System` |
| `Application/InstanceResource` | `App::InstanceResource` |

基盤側テンプレートの特殊化(`Engine::ECS::ComponentTraits<T>` など)は `Engine::ECS` を囲む位置でしか書けないので、
Namespace を閉じてから完全修飾の型名で書く。

```cpp
namespace App::Component
{
    struct HealthComponent
    {
        float hp = 100.0f;
    };
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::HealthComponent>
{
    ...
};
```

App の Namespace の中からは `Component::HealthComponent` のように末尾の Namespace から書く。

### Core の取り込み

Core は Engine / App から修飾なしで使えるよう、各トップレベル Namespace へ取り込んである
(`Engine/EngineCommon.h`・`Application/AppCommon.h`)。取り込みは名前空間の別名と using 宣言で行い、
`using namespace Core` は使わない(`Core::GUID` と Windows の `::GUID` が区別できなくなるため)。

```cpp
namespace Engine
{
    namespace Math = Core::Math;
    using Core::GUID;
}
```

Editor は Engine の型をそのまま扱う道具なので、`Editor/EditorCommon.h` で `using namespace Engine` している。
Editor の中で GUID を書くときは `Core::GUID` と書く。

---

## 1.5 Function Naming

関数名は、その関数が行う操作を明確に表す名前にする。

### 基本的な操作

| 操作    | Prefix / 命名     | 例                       |
| ----- | --------------- | ----------------------- |
| 追加    | `Add`           | `AddEntity()`           |
| 削除    | `Remove`        | `RemoveEntity()`        |
| 作成    | `Create`        | `CreateSoundInstance()` |
| 取得(無ければ作成) | `Request` | `RequestArchetype()`    |
| 提出・登録 | `Submit`        | `SubmitCommand()`       |
| 予約追加  | `ReserveAdd`    | `ReserveAddEntity()`    |
| 予約削除  | `ReserveRemove` | `ReserveRemoveEntity()` |
| 予約の処理 | `ApplyReserved` | `ApplyReservedFrees()`  |

即時に実行される操作と、後で実行される操作は名前から区別できるようにする。

```cpp
AddEntity();
RemoveEntity();

ReserveAddEntity();
ReserveRemoveEntity();

SubmitCommand();
```

### Request(取得、無ければ作成して取得)

`Request` は、求めるものがあればそれを返し、無ければ作ってから返す操作に使う。
呼んだ時点で結果(実体・参照・ハンドル)が返る。

```cpp
// キャッシュに無ければ PSO を作る
Handle<ID3D12PipelineState> RequestHandle(const GraphicsPipelineDesc& a_desc);

// 登録が無ければ読み込みを始め、どちらでも参照を返す
ResourceRef<T> RequestLoad(const Core::GUID& a_guid);

// シグネチャに合うアーキタイプが無ければ作る
Archetype* RequestArchetype(const Signature& a_sig);
```

呼ぶたびに新しく作るものは `Create` にする(`Request` にしない)。

### Reserve(予約)

`Reserve` は、頼まれたことをどこかに溜めておき、決まったタイミングでまとめて処理する操作に使う。
呼んだ時点ではまだ処理していないので、結果は返さない。後から追えるようにハンドルや ID をその場で返すのはよい。

溜めたものを処理する関数は `ApplyReserved` で始める。

```cpp
// フレームの区切りで World が消す
void ReserveRemoveEntity(const Entity& a_entity);

// 次のフレームの頭でシーンが切り替わる
void ReserveChangeScene(const Core::GUID& a_guid, const ESceneChangeType& a_changeType);

// GPU が使い終わってから空きへ戻す
void ReserveFree(const Handle<T>& a_handle);
void ApplyReservedFrees(UINT64 a_completedFenceValue);
```

容量の確保 `Reserve(n)`(`std::vector::reserve` と同じ意味)はこの規則の対象外。

---

## 1.6 Accessor Naming

アクセサは `Ref` と `Get` を使い分ける。

### Ref

`Ref` は内部データへの参照を返し、呼び出し側から内部データを直接変更できる。

```cpp
TextureDesc& RefTextureDesc();
```

### Get

`Get` は読み取り専用のアクセスを提供する。次のどちらかに当てはまるものだけを `Get` にする。

* `const` メンバ関数である
* 値のコピーを返す(受け取った側が書き換えても、内部は変わらない)

どちらでもないもの、つまり const でない関数が内部を書き換えられる参照やポインタを返すものは `Ref` にする。

戻り値は、データサイズや用途に応じて `const` Reference または Value を使用する。

```cpp
const TextureDesc& GetTextureDesc() const;      // const

UINT GetWidth() const;                          // const・値

EResourceState GetState(const Handle<T>& a_handle);  // 値のコピー(const でなくてもよい)

GraphicsCommandList* RefCurrentCmdList();       // 書き換えられるポインタを返す → Ref
```

`Get` から返したデータを通じて内部状態を変更できるようにしてはいけない。
ただし `const` メンバ関数が、持っているポインタの値(`ID3D12Resource*` など)をそのまま返すのはよい。

---

## 1.7 Public / Private Function

予約系の機能など、外部から直接呼び出す必要がない関数は `private` にする。

例えば、外部には `ReserveAdd()` だけを公開し、実際の `Add()` は予約処理内部からのみ呼び出す必要がある場合、

```cpp
class EntityManager
{
public:

    void ReserveAdd(const EntityDesc& a_desc);

private:

    void Add(const EntityDesc& a_desc);
};
```

のようにする。

**外部から使用する必要のない操作を、公開インターフェースに含めない。**

---

# 2. General Rules

## 2.1 Namespace

プロジェクト内で定義する型・関数・変数・定数などは、マクロを除き、必ず何らかの Namespace に属するものとする。

グローバル Namespace に直接定義を配置してはいけない。

```cpp
// NG
class RenderGraph
{
};

void Initialize();
```

```cpp
// OK
namespace Engine::Graphics
{
    class RenderGraph
    {
    };
}
```

マクロは Namespace によるスコープ管理ができないため、このルールの対象外とする。

---

## 2.2 Global Variables

グローバル変数は原則として使用しない。

Namespace 内のグローバル変数も原則として使用しない。

状態を共有する必要がある場合は、以下の方法を検討する。

* クラスのメンバ変数として管理する
* 必要なコンテキストを引数として渡す
* 適切な Manager / Context に状態を所有させる
* ECS の場合は Component として管理する

```cpp
// NG
namespace Engine::Graphics
{
    RenderContext* g_renderContext;
    UINT g_frameIndex;
}
```

---

## 2.3 Ownership

所有権を持つオブジェクトは、原則として `std::unique_ptr` で管理する。

所有権を明確にすることを優先し、`std::shared_ptr` は原則として使用しない。

`std::shared_ptr` は、遅延開放など、共有所有権が必要となる明確な理由がある場合に限って使用する。

単に「複数箇所からアクセスしたい」という理由だけで `std::shared_ptr` を使用してはいけない。

```cpp
// 基本
std::unique_ptr<Texture> m_texture;
```

```cpp
// 原則禁止
std::shared_ptr<Texture> m_texture;
```

`shared_ptr` を使用する場合は、共有所有権が必要となる理由を明確にする。

---

## 2.4 Non-owning Pointer

所有権を持たないオブジェクトへのアクセスには、Reference または Raw Pointer を使用する。

Raw Pointer を所有権管理の目的で使用してはいけない。

```cpp
class Renderer
{
private:

    // 非所有
    Device* m_device = nullptr;

    // 所有
    std::unique_ptr<RenderContext> m_context;
};
```

---

## 2.5 Forward Declaration

`.h` 側で Include が不要で、前方宣言によって対応できる場合は、可能な限り前方宣言を使用する。

```cpp
class Texture;
class RenderContext;
```

ただし、値型としてメンバに保持する場合など、完全型が必要な場合は Include する。

---

## 2.6 Const Correctness

変更しない値には、可能な限り `const` を付ける。

* 変更しない引数には `const` を付ける。
* 変更しないメンバ関数には `const` を付ける。
* コンパイル時に確定する値には `constexpr` を使用する。
* 読み取り専用のアクセサには `Get` を使用する。

---

## 2.7 RTTI

RTTI には依存しない。

Visual Studio 側の設定で RTTI を無効化しているため、以下の機能には依存しない。

* `dynamic_cast`
* `typeid`

型判別が必要な場合は、本プロジェクトで定義している Type ID 等の仕組みを使用する。

---

## 2.8 Error Handling

デバッグ・エラー処理は、原則としてプロジェクト共通の Debug 機能を使用する。

```text
Source/Core/Debug/DebugLog.h
```

通常のエラー処理では `assert` を使用せず、`ENGINE_ERRLOG` 系の機能を使用する。

ただし、以下は使用してよい。

* `static_assert`
* 型判別など、コンパイル時に検証するための `assert_v` 系

---

# 3. Include Rules

* `.h` は自身が直接使用する型に必要な Include を自分で持つ。
* 他のヘッダが Include していることを前提にしない。
* 前方宣言で済む場合は前方宣言を使用する。
* `.cpp` でのみ必要な Include は `.cpp` に置く。
* 不要な Include は追加しない。
* 間接 Include に依存したコードを書かない。

---

# 4. Dependency Rules

## 4.1 Layer Dependency

依存関係は基本的にトップダウンとする。

```text
Editor
  ↓
App
  ↓
Engine
  ↓
Core
```

### 許可

```text
App    → Engine
Editor → App
Editor → Engine
Engine → Core
App    → Core
Editor → Core
```

### 禁止

```text
Engine → App
Engine → Editor
App    → Editor
Core   → Engine / App / Editor
```

### Core に置くもの

App からも Engine からも使われる、ライブラリのような道具(数学・文字列・ファイル・型情報・GUID・ログ・アルゴリズムなど)。
Core は上の層の型を一切知らない。Engine の型(`Handle` など)に依存するものは Engine 側に置く(例 : `Engine/Utility/Pool`)。

### 上の層を呼びたいとき(窓口と組み立て)

下の層から上の層の機能を呼ぶ必要があるときは、下の層に窓口(インターフェース)を置き、上の層がそれを実装する。
実装を差し込むのは、全部の層を知っている最上位の `main.cpp`(組み立ての場所)だけ。

```cpp
// Engine : 窓口だけを知る
namespace Engine::DevTool { class IDevTool { public: virtual void Update(float a_deltaTime) = 0; ... }; }

// Editor : 実装する
class MainEditor : public Engine::DevTool::IDevTool { ... };

// main.cpp : つなぐ(持ち主も main.cpp)
auto _upEditor = std::make_unique<Editor::MainEditor>();
Engine::MainEngine::Instance().SetDevTool(_upEditor.get());
```

エディター(開発ツール)は `Engine::DevTool::IDevTool` を通してだけ呼ぶ。差し込まれていなければ nullptr なので、
呼ぶ側は必ず確かめる(Shipping ではエディター無しで動かすため)。

### 編集UIの窓口(EditorField)

コンポーネントの Edit・オプション・ゲームオブジェクトなど、エディターの外から編集UIを組むときは
`Engine::EditorField` の関数だけを使う(`ImGui::` を直接書かない)。
宣言は `Engine/EditorField` に置き、ImGui を使う実装は `Editor/Helper/EditorField.cpp` に置く。
App / Engine は Editor のヘッダーを include しない。

---

## 4.2 Data Passing

クラス間の依存を直接増やすのではなく、可能な限り Context / 引数を使用して必要なデータを上位から下位へ渡す。

```text
上位
 ↓
Context
 ↓
下位
```

下位レイヤーから上位レイヤーへアクセスするための参照を持たせない。

---

# 5. PCH

`Pch.h / Pch.cpp` を各分類のトップとして使用する。各 .cpp は自分の分類の PCH を強制インクルードする。

## 5.1 Common PCH

トップレベルの PCH には、プロジェクト全体で頻繁に使用する基本的なヘッダのみを登録する。

## 5.2 Category PCH

各分類では、トップの PCH を Include した上で、その分類で頻繁に使用するヘッダを分類別 PCH に登録する。

特定のクラスでしか使用しないヘッダは、各 `.h / .cpp` 側で直接 Include する。

| PCH | 使う .cpp | 中身 |
| --- | --- | --- |
| `Pch.h` | 外部ライブラリの .cpp(imgui など) | STL / Windows / DirectX / 外部ライブラリ |
| `Core/CorePCH.h` | `Source/Core` | `Pch.h` + `Core/Core.h` |
| `Engine/EnginePCH.h` | `Source/Engine`(vcxproj の既定) | `Pch.h` + `Engine/EngineCommon.h` |
| `Application/AppPCH.h` | `Source/Application`・`main.cpp` | `Pch.h` + `EngineCommon.h` + `Application/AppCommon.h` |
| `Editor/EditorPCH.h` | `Source/Editor` | `Pch.h` + `EngineCommon.h` + `AppCommon.h` + ImGui + `Editor/EditorCommon.h` |

* 各 PCH は `XxxPCH.cpp`(Create)で作り、`$(IntDir)XxxPCH.pch` に出す。
* .cpp を足したら、vcxproj で `PrecompiledHeaderFile` / `PrecompiledHeaderOutputFile` / `ForcedIncludeFiles` を分類の PCH にする
  (既定は EnginePCH。分類と PCH が合っていないとビルド時の `CheckPch` で止まる)。
* 分類の外から読まれるヘッダーは PCH に頼らない。上の層のヘッダーを読むときは、その層の共通ヘッダー
  (`AppCommon.h` など)を自分で Include する(例 : `Application/Object/UI/Decoration/Decoration.h`)。

---

# 6. Build Configuration

## 6.1 DebugMode

* デバッグ機能を最大限有効化する。
* DirectX 12 Debug Layer を有効化する。
* GPU Validation を有効化する。
* 重いモデル・テクスチャ等を除き、可能なものは JSON から読み込む。

## 6.2 Development

* Editor を有効化する。
* Profiler 等の開発ツールを有効化する。
* 重いモデル・テクスチャ等を除き、可能なものは JSON から読み込む。

## 6.3 Shipping

※ 現在未実装。

* リリース用 Build とする。
* すべてのデバッグ機能を除外する。
* Editor 機能を除外する。
* すべてのリソースをバイナリ形式で読み込む。

---

# 7. Comments

## 7.1 基本方針

コメントは、コードを見れば分かる内容ではなく、以下を中心に記述する。

* 役割
* 意図
* 設計上重要な情報
* なぜその実装になっているのか

コメントは長くなりすぎないようにする。

---

## 7.2 Class / Struct

クラス・構造体には役割を説明するコメントを付ける。

```cpp
//===========================================
// RenderGraph の実行単位を管理する。
//===========================================
class RenderGraph
{
};
```

---

## 7.3 Class / Struct Internal

クラス・構造体内部は、役割ごとに段落を分ける。

```cpp
//-------------------------------------------
// Pass管理
//-------------------------------------------

std::vector<RenderPass> m_passes;

//-------------------------------------------
// リソース管理
//-------------------------------------------

std::vector<GraphResource> m_resources;
```

段落分けが不要な場合は無理に追加しない。

---

## 7.4 Function

関数には何をする関数なのかを簡潔に記述する。

```cpp
// テクスチャを作成する。
void CreateTexture();
```

コードそのものを説明するコメントは避ける。

---

## 7.5 Member Variables

メンバ変数には、その変数が何を表すのかを必要に応じて記述する。

変数名から明らかな場合は、無理にコメントを付けない。

---

## 7.6 Static Functions

Static 関数も通常の関数と同様に、何をする関数なのかを `//` で簡潔に記述する。

---

## 7.7 Global Functions

Namespace 内のクラス・構造体に属さず、ヘッダを Include するだけで外部から利用できる関数は `///` を使用してドキュメント形式で記述する。

```cpp
/// テクスチャを読み込む。
///
/// @param a_path テクスチャのパス。
/// @return 読み込んだテクスチャ。
Texture LoadTexture(const std::string& a_path);
```

---

# 8. Class Layout

クラスの宣言は、原則として以下の順序で記述する。

```cpp
class Hoge
{
public:

    // 公開関数群

private:

    // 内部関数群

private:

    // メンバ変数
};
```

不要なセクションは省略してよい。

---

# 9. Directory Structure

ディレクトリは機能・役割ごとに分類する。

最上位は層ごとに分ける。

```text
Source/
├─ Core/          Core      : どの層にも依存しない道具箱(Core.h が中心)
├─ Engine/        Engine    : エンジン本体(EngineCommon.h / MainEngine.h が中心)
├─ Application/   App       : ゲーム(AppCommon.h / App.h が中心)
└─ Editor/        Editor    : エディター(Editor.h / EditorCommon.h が中心)
```

各層の中も機能ごとに分ける。

```text
Engine/
├─ MainEngine.h
├─ MainEngine.cpp
│
├─ Graphics/
├─ ECS/
└─ Scene/
```

各ディレクトリには、その分類の中心となる `.h / .cpp` を配置する。

中心となるクラスの機能をさらに細分化する場合は、その `.h / .cpp` と同じ階層にディレクトリを作成し、その下に関連する機能を配置する。

ディレクトリ構成は、可能な限り以下を表現する。

* 機能の分類
* クラスの所有関係
* 依存関係
* システムの階層

依存関係は、可能な範囲で横方向ではなく上下方向で表現する。

```text
上位機能
  ↓
サブシステム
  ↓
具体的な実装
```

---

# 10. ECS Rules

## 10.1 Component

* Component は原則としてデータのみを保持する。
* Component にゲームロジックを持たせない。
* Component 間の処理依存を作らない。
* Component は可能な限り Trivially Copyable を維持する。
* Component から他の Component へのポインタを保持しない。

## 10.2 System

* System は Component のデータを処理する。
* System ごとに責務を明確にする。
* 複数の責務を持つ System は分割を検討する。
* System 間の実行順序が必要な場合は依存関係を明示する。

---

# 11. RenderGraph Rules

* Render 処理は可能な限り RenderGraph Pass として定義する。
* Pass は単一の責務を持つ。
* Resource の Read / Write を Pass に明示する。
* Resource State Transition は RenderGraph 側で管理する。
* Pass 内部で他 Pass の実行順序を直接制御しない。
* Pass 間の依存関係は Resource Access によって表現する。
* 1 Pass = 1 Shader Dispatch / Draw 単位を基本とする。
