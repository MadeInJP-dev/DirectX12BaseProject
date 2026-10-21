#pragma once

#include "../../../../Internal/EditorContext.h"
#include "Editor/EditorCommon.h"

namespace Engine::ECS { class World; }
namespace Engine::Resource { class Prefab; }

namespace Editor::Inspector
{
	/// モデル
	void ModelDraw(EditorContext& a_editContext);

	/// テクスチャ
	void TextureDraw(EditorContext& a_editContext);

	/// ステートマシン
	void AnimatorDraw(EditorContext& a_editContext);

	/// パーティクル
	void ParticleDraw(EditorContext& a_editContext);

	/// マテリアル
	void MaterialDraw(EditorContext& a_editContext);

	/// メッシュ
	void MeshDraw(EditorContext& a_editContext);

	/// アニメーション
	void AnimationDraw(EditorContext& a_editContext);

	/// シェーダー
	void ShaderDraw(EditorContext& a_editContext);

	// シェーディングモデル

	/// プレハブ
	void PrefabDraw(EditorContext& a_editContext);

	/// プレハブのコンポーネントの羅列・編集・追加。
	/// プレハブ / エフェクトプレハブ / エフェクトエディター / プレハブエディターで同じものを使う
	/// (片方だけ直し忘れないように、UIは1か所にしておく)
	/// a_nodeIndex : 触るノード。既定はルート(Resource::Prefab::ROOT_NODE)、0 以上は子の添え字
	void PrefabComponentsEdit(ECS::World* a_pWorld, Resource::Prefab* a_pPrefab, int a_nodeIndex = -1);

	/// エフェクトプレハブ
	void EffectPrefabDraw(EditorContext& a_editContext);

	/// オーディオビヘイビア
	void AudioBehaviorDraw(EditorContext& a_editContext);

	/// エフェクト
	void EffectAssetDraw(EditorContext& a_editContext);

	/// UIマテリアル
	void UIMaterialDraw(EditorContext& a_editContext);

	/// UIプリセット
	void UIPresetDraw(EditorContext& a_editContext);

	// レンダリングパイプライン(レンダーグラフの設計図)は
	// ノードエディターの状態を持つので RenderingPipelineEditor 側にある
}