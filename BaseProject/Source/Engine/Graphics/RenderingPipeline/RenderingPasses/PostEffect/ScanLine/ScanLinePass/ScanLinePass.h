#pragma once

#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"

namespace Engine::Graphics::Pipeline
{
	// 走査線の調整値(エディターで触るぶん)。
	// 経過時間はパスが数えるので、ここには持たない
	struct ScanLineParams
	{
		float opacityScanLine = 0.1f;		// 走査線の濃さ(0で出ない)
		float opacityNoise = 0.05f;			// ノイズの濃さ(0で出ない)
		float flickeringSpeed = 10.0f;		// 明滅の速さ
		float flickeringStrength = 0.02f;	// 明滅の強さ(0で明滅しない)
		int   enable = 1;					// 0 ならそのまま通す(全体)

		// 効果ごとの有効/無効。0 ならその効果だけ掛けない
		int   enableScanLine = 1;			// 走査線
		int   enableNoise = 1;				// ノイズ
		int   enableFlickering = 1;			// 明滅
	};

	//======================================================================================
	// ScanLinePass
	//
	// 入力の絵に走査線・ノイズ・明滅を掛けて、古いモニター越しのような絵にする。
	//
	// Mask を繋ぐと、そのマスクが立っているところにだけ掛ける。
	// UIPass の ScanLineMask を繋げば、スキャンラインを選んだUIにだけ掛かる
	// (このときシーンには掛からないので、シーンにも掛けたいなら UIPass より前にもう1つ置く)
	//
	// 調整値はこのパスのメンバとして持つ(パイプラインごとに違う設定にできる)
	//======================================================================================
	class ScanLinePass : public Pass
	{
	public:
		~ScanLinePass() override = default;

		void SetupSlots() override;

		void Compile(const PassContext& a_context) override;
		void Update(const PassContext& a_context) override;


		void Archive(Engine::Persistence::Archive& a_arch) override;

		// 編集対象の値 : エディターはここだけを触る
		using Params = ScanLineParams;
		Params& RefParams() { return m_params; }

	private:

		// ルートパラメータの番号 : シェーダー(ScanLineCS)の並びと合わせる
		static constexpr int ROOT_SCAN_LINE_CB = 0;
		static constexpr int ROOT_COLOR_SRV = 1;
		static constexpr int ROOT_OUTPUT_UAV = 2;

		ScanLineParams m_params;

		// パスが回り始めてからの経過時間(秒)。
		// 実行インスタンスごとに持つので、パイプラインを組み直すと 0 から数え直す
		float m_elapsedTime = 0.0f;
	};
}
