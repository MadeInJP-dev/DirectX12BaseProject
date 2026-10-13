#pragma once
#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"

namespace Engine::Graphics::Pipeline
{
	//======================================================================================
	// SceneVolumetricFogPass
	//
	// カメラから見えている面(空なら maxDistance 先)までのレイに沿って、2つの媒質を積分する。
	//   ・シーンのフォグ   : シーン全体に一様に漂う(SceneFogCB)
	//   ・グラウンドダスト : 地面から一定の高さまで漂うチリ(GroundDustCB)
	// 出力 : rgb = フォグの色(2つの媒質の色を濃さで混ぜたもの) / a = フォグの濃さ(0..1)
	//
	// ダストは見えている面が地面そのものの画素にだけ置く(GroundDepthPass の地面だけの深度と比べる)。
	// 手前に物体がある画素・空の画素はシーンのフォグだけ。
	// 衝撃でチリが払われる・波頭へ寄せられる量は GroundFieldPass が書いたテクスチャを引く。
	// 地面の深度を繋がなければダストは出ず、フィールドを繋がなければチリは衝撃で動かない。
	//
	// 媒質は環境光と平行光(主光源)で照らす。平行光の影はレイに沿って引くので、窓や木漏れ日から光の筋が差す。
	// 影の求め方はシーンの設定(LightManager::GetShadowMode)に合わせる。
	//   ShadowMap  : ShadowMapPass のシャドウマップ(CSM)を引く(ShadowMap 入力を繋いだときだけ)
	//   Raytracing : RaytracingVolumeShadowPass が作った、視線に沿った日なたの割合(VolumeShadow 入力)を使う。
	//                フォグの中ではレイを飛ばさない(レイトレはレイトレのパスが受け持つ)
	// どちらも引けないフレーム(どちらの入力も繋がっていない)は、平行光を遮らずに照らす。
	// 影を引くのはシーンの影の距離(ShadowDistance)まで。その先は遮らない。
	//
	// フォグは低周波なので、描画解像度より小さく回せる(既定 1/2)。
	// フォグの1画素は、覆う範囲の中の代表の1画素を通るレイを積分する。
	// 合成(SceneFogCompositePass)は代表の深度を見ながら引き伸ばすので、
	// 合成のパスにもシーンの深度を繋ぐこと(繋がないと物体の縁にフォグがにじむ)
	//
	// 値(フォグ・ダストの調整値とノイズテクスチャ)はシーン(Engine::Scene::SceneAmbient)の
	// 持ち物で、パスは SceneView から受け取って送るだけ。パス自身は設定を持たない。
	// ノイズテクスチャはグラフのリソースではないので、スロットには乗らない。
	// シーンが読み込んだものの番号を直接渡す(未設定・読み込み中ならノイズなし)
	//======================================================================================
	class SceneVolumetricFogPass : public Pass
	{
	public:
		~SceneVolumetricFogPass() override = default;

		void SetupSlots() override;

		void Compile(const PassContext& a_context) override;
		void Update(const PassContext& a_context) override;


		void Archive(Engine::Persistence::Archive& a_arch) override;

		struct Params
		{
			// 描画解像度の何分の1で回すか(1 / 2 / 4)
			uint32_t resolutionDivisor = 2;
		};

		// 編集対象の値 : エディターはここだけを触る
		Params& RefParams() { return m_params; }

		// resolutionDivisor を出力スロットの大きさへ写す。変えたらグラフの組み直しが要る
		void ApplyResolution();

	private:

		// ルートパラメータの番号 : シェーダー(SceneVolumetricFogCS)の並びと合わせる
		static constexpr int ROOT_CAMERA_CB = 0;
		static constexpr int ROOT_SCENE_FOG_CB = 1;
		static constexpr int ROOT_GROUND_DUST_CB = 2;
		static constexpr int ROOT_INPUT_SRV = 3;
		static constexpr int ROOT_OUTPUT_UAV = 4;
		static constexpr int ROOT_NOISE_SRV = 5;
		static constexpr int ROOT_AMBIENT_CB = 6;
		static constexpr int ROOT_SHADOW_CB = 7;
		static constexpr int ROOT_SUN_LIGHT_CB = 8;

		// ノイズが張られていないときに渡す番号(シェーダーの DESCRIPTOR_INDEX_NONE と合わせる)
		static constexpr UINT DESCRIPTOR_INDEX_NONE = 0xFFFFFFFF;

		Params m_params = {};

		// パスが回り始めてからの経過時間(秒)。
		// 実行インスタンスごとに持つので、パイプラインを組み直すと 0 から数え直す
		float m_elapsedTime = 0.0f;
	};
}
