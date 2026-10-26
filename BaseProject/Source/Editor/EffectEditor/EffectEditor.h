#pragma once

#include "Editor/EditorCommon.h"
#include "../Widget/ScenePreview/IPreviewEditor.h"
#include "../Widget/ScenePreview/ScenePreview.h"
#include "../Widget/PopUp/Popup.h"

namespace Engine
{
	namespace Resource
	{
		class EffectAsset;
		class EffectPrefab;
		class ParticlesAsset;
	}
	namespace Scene { class SceneAmbient; }
}

namespace Editor
{
	//======================================================================================
	//
	// エフェクトエディター
	//
	// エフェクトアセット1枚を、ゲームと同じ描画環境で「見ながら組む」ための画面。
	// アセットインスペクターの「Open Effect Editor」から開く。
	//
	// ・確認用の専用ワールド(エディターシーン)を1つ持つ。
	//   中身は編集中のエフェクトの実体1つだけ。周りに何も無いので、
	//   ゲームのシーンに置いて他のものに埋もれる前に、単体で見え方を詰められる。
	//
	// ・描画は「ゲームのシーンと同じレンダーグラフ」をそのまま通す。
	//   別のパスや簡易プレビューを用意していないのは、そうすると
	//   トーンマップやブルームの掛かり方が本番と変わり、ここで合わせた見た目が
	//   ゲームに持っていくとずれるため。開いている間はゲームのシーンを止めて、
	//   このワールドの描画命令だけをレンダーグラフへ流す。
	//
	// ・背景(環境設定)は上段の Background で選ぶ。
	//     Black        : 空・フォグ無しの黒背景に格子だけ(既定)。薄い粒や加算の抜けが
	//                    空の色に紛れないので、エフェクト単体の見え方を詰めるとき用
	//     CurrentScene : 開く前のシーンの環境設定(空・フォグ・光)そのまま。
	//                    置く先の景色の中でどう見えるかを確かめるとき用
	//   流し込むのは SceneManager(GetPreviewAmbient で選んだものを毎フレーム渡す)。
	//
	// ・右側で編集する。中身はアセットインスペクターとまったく同じ関数
	//   (EffectAssetEdit / ParticleEdit)を呼んでいるだけで、UIを二重に持っていない。
	//     Effect   タブ : パーツの足し引き・発生位置・発生数・時間指定
	//     Particle タブ : そのパーツが使っている粒そのもの(初速・寿命・絵)
	//   粒まで同じ画面で触れないと、「もう少し細く」のたびに画面を往復することになる。
	//
	// ・カメラはシーンビューと同じ EditorCamera(右クリック中だけ操作するフリーカメラ)。
	//   操作感を変えると、こちらで合わせた画角をシーンで作り直すことになるため。
	//
	// ・開いている間はモーダル。他のパネルもアプリのモード切り替え(O/P)も受け付けない。
	//   ゲームのシーンが止まっている状態でモードを切り替えられると、
	//   「プレイモードなのに何も動かない」という筋の通らない状態になるため。
	//
	// ・エフェクトプレハブ(EffectPrefab)も開ける(OpenEffectPrefab)。
	//   原点に炊き、全部が寿命で消えたら(Loop なら)炊き直す。
	//   右側はプレハブと同じコンポーネント編集。変更は次に炊いたときから効く(Restart)。
	//   プレビューのワールドには地面が無いので、着地する破片は寿命まで落ち続ける。
	//
	//======================================================================================
	class EffectEditor : public IPreviewEditor
	{
	public:

		// a_pServices : アプリ寿命のサービス一式(借り物)。アセットの読み込みに使う
		explicit EffectEditor(ECS::EngineServices* a_pServices);
		~EffectEditor() override;
		NON_COPYABLE_NON_MOVABLE(EffectEditor);

		/// <summary>
		/// 指定のエフェクトを開く(次のフレームからプレビューが回りだす)
		/// </summary>
		void Open(const Core::GUID& a_effectGUID);

		/// <summary>
		/// 指定のエフェクトプレハブを開く(原点に炊き、消えたら炊き直す)
		/// </summary>
		void OpenEffectPrefab(const Core::GUID& a_effectPrefabGUID);

		/// <summary>
		/// 閉じる。ワールドは残し、中に出したエフェクトだけを片付ける
		/// </summary>
		void Close();

		//----------------------------------------------------------------------------------
		// IPreviewEditor
		//----------------------------------------------------------------------------------
		bool IsOpen() const override { return m_isOpen; }
		void UpdateScene(float a_dt) override;
		void DrawScene() override;
		void UpdateCamera(float a_dt) override;
		bool TryGetCameraOverride(Math::Matrix& a_outWorldMat, Math::Matrix& a_outProjMat) const override;
		void OnDrawImGui() override;
		void Release() override;

		// 背景 : Black なら空・フォグ無しのもの、CurrentScene ならゲームのシーンのもの(nullptr)
		const Engine::Scene::SceneAmbient* GetPreviewAmbient() const override;

	private:

		// 編集中のエフェクトの実体を出す / 片付ける
		void ReserveSpawn();
		void DestroyEffectEntity();

		// ワールドの中からプレビュー中のエフェクトを引く。まだ実体化していなければ nullptr
		struct EffectRef;
		EffectRef FindEffect() const;

		// 編集対象のアセット。読み込めていなければ nullptr
		Resource::EffectAsset* RefEffectAsset() const;
		Resource::EffectPrefab* RefEffectPrefab() const;

		// 開くときの共通の後半(プレビューの用意と最初の生成)
		void BeginOpen();

		// エフェクトプレハブで出したもののうち、まだ残っている数(寿命を持つもの)
		int CountPrefabEntities() const;

		// エフェクトプレハブの編集欄
		void DrawEffectPrefabEditPane();

		// 今 Particle タブで開いているパーツの粒。選んでいなければ nullptr
		Resource::ParticlesAsset* RefSelectedParticleAsset() const;

		// UI
		void DrawContent();
		void DrawToolbar();
		void DrawInfo();
		void DrawEditPane();
		void DrawParticleTab();

		// プレビューの持ち主の位置と向きを、上の設定から書き込む(エフェクトのときだけ)
		void ApplyPreviewTransform(ECS::Entity a_entity, float a_dt);

	private:

		//----------------------------------------------------------------------------------
		// 状態
		//----------------------------------------------------------------------------------
		bool m_isOpen = false;			// 開いているか

		// 開いているもの。m_effectGUID はどちらのモードでも「開いているアセット」のGUID
		enum class EMode
		{
			Effect,			// EffectAsset
			EffectPrefab,	// EffectPrefab
		};
		EMode m_mode = EMode::Effect;

		Core::GUID m_effectGUID = Core::DEFAULT_GUID;
		Engine::Handle<Resource::EffectAsset> m_effectHandle = {};
		Engine::Handle<Resource::EffectPrefab> m_effectPrefabHandle = {};

		float m_prefabElapsed = 0.0f;	// エフェクトプレハブを炊いてからの経過時間(秒)

		// アプリ寿命のサービス一式(借り物)
		ECS::EngineServices* m_pServices = nullptr;

		// 確認用のワールド・カメラ・描画構成・再生と格子
		ScenePreview m_preview;

		// この画面そのもの(画面の大部分を使うモーダル)
		Popup m_popup;

		//----------------------------------------------------------------------------------
		// 再生
		//----------------------------------------------------------------------------------
		bool  m_isLoop = true;			// 出し切ったら頭から繰り返す
		bool  m_isRestartRequest = false;

		//----------------------------------------------------------------------------------
		// 編集
		//----------------------------------------------------------------------------------
		int m_selectedParticlePart = 0;	// Particle タブで開いているパーツ番号
		float m_editPaneWidth = 420.0f;	// 右の編集欄の幅(px)

		//----------------------------------------------------------------------------------
		// 表示
		//----------------------------------------------------------------------------------

		// 背景(環境設定)。閉じても覚えておき、次に開いたときも同じ背景で見る
		enum class EBackground
		{
			Black,			// 空・フォグ無しの黒背景(SceneAmbient::CreatePlain)
			CurrentScene,	// 開く前のシーンの環境設定そのまま
		};
		EBackground m_background = EBackground::Black;

		// 黒背景のときに流し込む環境設定
		std::unique_ptr<Engine::Scene::SceneAmbient> m_upPlainAmbient = nullptr;

		//----------------------------------------------------------------------------------
		// プレビューの持ち主(エフェクトを付けたエンティティ)の動かし方
		//
		// アセットは持ち主の座標系で作るので、持ち主を回したときに向きが付いてくるか、
		// 動かしたときに Local の粒が付いてきて World の粒が置き去りになるかを、ここで確かめる
		//----------------------------------------------------------------------------------
		Math::Vector3 m_previewRotation = { 0.0f, 0.0f, 0.0f };	// 持ち主の向き(度。Pitch / Yaw / Roll)
		bool  m_isPreviewOrbit = false;		// 原点のまわりを回す
		float m_previewOrbitRadius = 2.0f;	// 回す半径(m)
		float m_previewOrbitSpeed = 90.0f;	// 回す速さ(度/秒)
		float m_previewOrbitAngle = 0.0f;	// いまの角度(度)

	};
}
