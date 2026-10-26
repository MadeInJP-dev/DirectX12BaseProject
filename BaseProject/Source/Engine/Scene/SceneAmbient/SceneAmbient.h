#pragma once

#include "Engine/Graphics/LightManager/Core/Light.h"	// 平行光の実体はここの型
#include "Engine/Graphics/LightManager/Core/Shadow.h"	// 平行光の影の設定
#include "Engine/Graphics/Frame/SceneView/AmbientData.h"
#include "Engine/Graphics/Frame/SceneView/SceneFogData.h"
#include "Engine/Graphics/Frame/SceneView/SkyData.h"
#include "Engine/Scene/SceneAmbient/SceneAmbientMood.h"

namespace Engine
{
	namespace ECS
	{
		struct EngineServices;
	}
	namespace Graphics
	{
		class GraphicsEngine;
	}
	namespace Resource
	{
		class ResourceManager;
		class Texture;
	}
}

namespace Engine::Scene
{
	//======================================================================================
	// シーンの環境設定
	//
	// 環境光・平行光・影・高さ/距離フォグ・ボリュメトリックフォグ・空を、
	// シーン(BaseScene)の持ち物として一か所にまとめたもの。シーンと一緒に保存される。
	//
	// ・なぜシーンが持つか
	//     パスに持たせると同じパイプラインを使うシーンで値を変えられず、
	//     シーンに置くオブジェクトに持たせると置き忘れ・二重置きが起きる。
	//     シーンそのものの性質なのでシーンが固有で持つ。
	//
	// ・パスは受け取るだけ
	//     値は SceneManager が毎フレーム GraphicsEngine(SceneView / LightManager)へ流し込み、
	//     各レンダーパスはそこから読む。パス側に調整値は無い。
	//
	// ・重ねたシーン
	//     流し込むのは「環境設定を使う(isEnabled)」シーンのうち一番上のもの。
	//     ポーズ画面のように後ろのシーンの見た目をそのまま使いたいシーンは切っておく。
	//
	// ・テクスチャの所有はこちら
	//     GraphicsEngine へ渡すのはハンドルだけ。シーンが消えるときは SceneManager が
	//     流し込み直して、消えたシーンのハンドルを残さない。
	//
	// ・平行光の席(LightManager のハンドル)はここでは持たない
	//     流し込む側(SceneManager)が1つだけ持つ。シーンごとに持つと、
	//     重ねたシーンの数だけ太陽が並んでしまう。
	//======================================================================================
	class SceneAmbient
	{
	public:

		// 置いた直後から絵になるよう、平行光だけ既定値を入れておく
		SceneAmbient();

		// 確認用の素の環境(エフェクトエディターの黒背景など)。
		// 空・フォグ・チリは無し(背景は黒)。物が見えるよう平行光と環境光だけ入れる
		static SceneAmbient CreatePlain();

		//----------------------------------------------------------------------------------
		// 保存・読み込み
		//
		// 読み込み後は RequestLoadAssets でテクスチャの読み込みを始めさせること
		// (実体が届くのは待たない。届くまでの間、空は描かれず、フォグはノイズなしになる)
		//----------------------------------------------------------------------------------
		void Archive(Persistence::Archive& a_ar);
		void RequestLoadAssets(Resource::ResourceManager& a_resourceManager);

		//----------------------------------------------------------------------------------
		// GraphicsEngine へ流し込む
		//
		// a_inoutDLHandle : 平行光の席。無効なら取る(取れなければそのフレームは平行光なし)
		// a_mood          : 演出(既定は何も掛けない)。シーンの値に混ぜて流す
		//----------------------------------------------------------------------------------
		void Apply(Graphics::GraphicsEngine& a_ge, Handle<Graphics::DirectionalLight>& a_inoutDLHandle,
			const SceneAmbientMood& a_mood = {}) const;

		// 環境設定を使うシーンが無いとき : 環境光・フォグ・空を消し、平行光の席を返す
		static void ApplyNone(Graphics::GraphicsEngine& a_ge, Handle<Graphics::DirectionalLight>& a_inoutDLHandle);

		//----------------------------------------------------------------------------------
		// 編集UI(エディターの SceneSettingsPanel から呼ばれる)
		//----------------------------------------------------------------------------------
		void DrawEdit(const ECS::EngineServices& a_services);

		// このシーンの環境設定を使うか。
		// 切ると、下に重なっているシーンの環境設定がそのまま使われる
		bool IsEnabled() const { return m_isEnabled; }

	private:

		// 編集UIの各セクション
		void DrawLightingEdit();
		void DrawShadowEdit();
		void DrawFogEdit();
		void DrawVolumetricFogEdit(const ECS::EngineServices& a_services);
		void DrawSkyEdit(const ECS::EngineServices& a_services);

	private:

		//-------------------------------------------------------------------
		// 設定(保存される)
		//-------------------------------------------------------------------
		bool m_isEnabled = true;

		// 環境光・高さフォグ・距離フォグ。定数バッファそのままの形で持つ
		// (シェーダーへ送る単位と分けても、二重に持ち替えるだけなので合わせてある)
		Graphics::AmbientData m_ambient = {};

		//---------------------------------------------------------------------------------
		// 平行光(太陽)
		//
		// 影(RaytracingShadowPass / ShadowMapPass)とGI(RaytracingGIPass)がレイを飛ばす先と、
		// ディファードが足す光は同じ1本を前提にしている。値はここが持ち、
		// 実体は LightManager の席へ毎フレーム流し込む。
		//---------------------------------------------------------------------------------
		Math::Vector3 m_dlDir = { 0.5f, -1.0f, 0.5f };		// 向き(光の進む向き)
		Math::Vector3 m_dlColor = { 4.0f, 4.0f, 4.0f };		// 色(1.0超え可)

		// 平行光の影 : レイトレかシャドウマップか、とシャドウマップの調整値。
		// シーンごとに持つので、ホーム画面はレイトレ・ゲーム中はシャドウマップ、と使い分けられる
		Graphics::DirectionalShadowSettings m_shadow = {};

		// ボリュメトリックフォグ(SceneVolumetricFogPass / SceneFogCompositePass が受け取る)
		Graphics::SceneFogCB m_sceneFog = {};					// シーン全体に一様に漂うフォグ
		Graphics::GroundDustCB m_groundDust = {};				// 地面から立つチリ(time はパスが進める)
		Graphics::SceneFogCompositeCB m_fogComposite = {};		// メインカラーへ重ねるときの強さ
		Core::GUID m_fogNoiseTexGUID = {};					// ダストに掛けるノイズ。未設定ならノイズなし

		// 空の見え方(露出 / 地平線の高さ / 仮想ドームの半径 / 方位の回転 / 被写界深度)
		Graphics::SkyData m_sky = {};

		// スカイテクスチャ(正距円筒。横:縦 = 2:1 のもの)
		Core::GUID m_skyTexGUID = {};

		//-------------------------------------------------------------------
		// 状態(保存しない)
		//-------------------------------------------------------------------
		// テクスチャの実体はこちらが握る。GraphicsEngine へはハンドルだけ貸す
		ResourceRef<Resource::Texture> m_skyTexRef = {};
		ResourceRef<Resource::Texture> m_fogNoiseTexRef = {};
	};
}
