#pragma once

#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"

namespace Engine::Graphics::Pipeline
{
	//======================================================================================
	// DamageEffectPass
	//
	// プレイヤーが被弾したときだけ掛かる画面効果。普段は絵をそのまま通す。
	//
	// 画面(シーン)と HUD(UI)を別々に受け取り、それぞれに
	//   ・砂嵐のノイズ
	//   ・横の帯ずれ(グリッチ)
	//   ・RGB のずれ(色収差)
	// を掛ける。画面にはさらに色抜けと縁の染め、HUD にはちらつきを掛けられる。
	// 2枚のまま出すので、合成は後ろの BlendPass に任せる。
	//
	//   ToneMap ─ Color ┐          ┌ Color ─ BlendPass.Color
	//   UI 系 ── UI ────┴ Damage ──┴ UI ──── BlendPass.BlendColor
	//
	// 値は持たない。ゲーム側が SceneView::SetDamageEffectData で毎フレーム入れたものを読む
	// (入れなかったフレームは掛かり具合 0 = 素通し)
	//======================================================================================
	class DamageEffectPass : public Pass
	{
	public:
		~DamageEffectPass() override = default;

		void SetupSlots() override;

		void Compile(const PassContext& a_context) override;
		void Update(const PassContext& a_context) override;


		void Archive(Engine::Persistence::Archive& a_arch) override;

	private:

		// ルートパラメータの番号 : シェーダー(DamageEffectCS)の並びと合わせる
		static constexpr int ROOT_DAMAGE_CB = 0;
		static constexpr int ROOT_INPUT_SRV = 1;		// Color / UI
		static constexpr int ROOT_OUTPUT_UAV = 2;		// Color / UI

		// ノイズとずれを動かす経過時間(秒)
		float m_elapsedTime = 0.0f;
	};
}
