#include "MouseCursor.h"

#include "Engine/MainEngine.h"
#include "Engine/Window/NativeWindow.h"
#include "Engine/Input/InputManager/InputManager.h"
#include "Engine/Config/ConfigRegistry.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/Texture/Texture.h"

namespace App::Game
{
	namespace
	{
		// エンジン設定での保存のグループ名
		constexpr const char* CONFIG_NAME = "CursorConfig";
	}

	void MouseCursor::Init(const Engine::ECS::EngineServices* a_pServices)
	{
		m_pServices = a_pServices;

		// 設定はエンジン設定のファイルに一緒に置く(登録した時点で保存されている値が入る)
		if (m_pServices && m_pServices->pConfigRegistry)
		{
			m_pServices->pConfigRegistry->Register(CONFIG_NAME, "App::Game::MouseCursor", Engine::Config::EConfigCategory::Game, m_config);
		}

		// 実際の読み込み要求は Update で出す。
		// 設定はエディターから触れるので、初回だけでなく「変わったら読み直す」形に
		// 寄せておいたほうが分岐が一箇所で済む
		m_texRef = {};
		m_loadedGUID = {};
		m_isHideOSCursor = false;
		m_isDraw = false;
	}

	void MouseCursor::Release()
	{
		// ResourceRef のデストラクタが参照を返すので、空を入れて手放す
		m_texRef = {};
		m_loadedGUID = {};
		m_isHideOSCursor = false;
		m_isDraw = false;

		// 設定は外す(値は登録先が覚えていて、エンジンの終了時に保存される)
		if (m_pServices && m_pServices->pConfigRegistry)
		{
			m_pServices->pConfigRegistry->Unregister(CONFIG_NAME);
		}

		// 消したまま終わらないように戻しておく
		if (m_pServices && m_pServices->pMainEngine)
		{
			if (auto* _pWindow = m_pServices->pMainEngine->RefNativeWindow())
			{
				_pWindow->SetCursorHidden(false);
			}
		}
	}

	//======================================================================================
	// 毎フレームの更新
	//======================================================================================
	void MouseCursor::Update()
	{
		if (!m_pServices || !m_pServices->pMainEngine) return;

		Evaluate();

		// OSのカーソルを消してよいかをウィンドウへ伝える(WM_SETCURSOR がこれを見る)。
		// エディターへ戻ったフレームで false に戻すのもここ
		if (auto* _pWindow = m_pServices->pMainEngine->RefNativeWindow())
		{
			_pWindow->SetCursorHidden(m_isHideOSCursor);
		}
	}

	void MouseCursor::Evaluate()
	{
		m_isHideOSCursor = false;
		m_isDraw = false;

		auto& _resourceManager = *m_pServices->pResourceManager;

		// 切られている / 画像が未設定なら、OSのカーソルをそのまま出す
		if (!m_config.isEnable || !m_config.textureGUID.IsValid())
		{
			if (m_loadedGUID.IsValid())
			{
				m_texRef = {};
				m_loadedGUID = {};
			}
			return;
		}

		// 設定が差し替わっていたら読み直す。
		// エディター中も読み込みだけは進めておき、ゲームへ切り替えた瞬間から出せるようにする
		if (!(m_loadedGUID == m_config.textureGUID))
		{
			m_texRef = _resourceManager.RequestLoad<Engine::Resource::Texture>(m_config.textureGUID);
			m_loadedGUID = m_config.textureGUID;
		}

		// 自前のカーソルはゲームモードの間だけ。
		// エディター(デバッグプレイを含む)ではOSのカーソルをそのまま使う
		if (m_pServices->pMainEngine->GetMode() != Engine::EAppMode::Game) return;

		// 読み込みが終わるまではOSのカーソルを消さない。
		// 消してから絵が出るまでの間、カーソルが1つも無い状態になってしまうため
		if (!_resourceManager.IsReady(m_texRef)) return;

		// ここまで来たら自前の絵を出せる
		m_isHideOSCursor = true;

		// 視点操作でカーソルを画面中央へ固定している間は絵を描かない。
		// 毎フレーム中央へ戻されるので位置に意味が無く、画面中央に矢印が
		// 貼り付いて見えるだけになる。OSのカーソルは消したままにしておく
		if (m_pServices->pInputManager->IsCursorLockActive()) return;

		// クライアント領域の外に出ているならこちらで描くものは無い
		// (OSのカーソルはそのウィンドウの上でしか消えないので、外は元から普通に出る)
		if (!TryGetCursorClientPos(m_clientPos)) return;

		m_isDraw = true;
	}

	//======================================================================================
	// カーソルのクライアント座標を取る
	//======================================================================================
	bool MouseCursor::TryGetCursorClientPos(Math::Vector2& a_outClientPos) const
	{
		Math::Vector2 _clientPos = {};
		if (!m_pServices->pInputManager->GetCursorClientPos(_clientPos)) return false;

		const auto* _pWindow = m_pServices->pMainEngine->GetNativeWindow();
		if (!_pWindow) return false;

		const float _clientW = static_cast<float>(_pWindow->GetClientWidth());
		const float _clientH = static_cast<float>(_pWindow->GetClientHeight());

		// 最小化中は0になる
		if (_clientW <= 0.0f || _clientH <= 0.0f) return false;

		// クライアント領域の外(ウィンドウの枠や他のウィンドウの上)は範囲外
		if (_clientPos.x < 0.0f || _clientPos.x >= _clientW) return false;
		if (_clientPos.y < 0.0f || _clientPos.y >= _clientH) return false;

		a_outClientPos = _clientPos;
		return true;
	}

	//======================================================================================
	// ゲーム画面へ描く
	//--------------------------------------------------------------------------------------
	// UIパスは深度を切ってあるので、積んだ順がそのまま前後になる。
	// 呼び出し元がUIを全部積み終えた後に呼ぶことで最前面になる。
	//======================================================================================
	void MouseCursor::SubmitUI(Engine::Graphics::DrawSubmitter* a_pDrawSubmitter) const
	{
		if (!m_isDraw || !a_pDrawSubmitter) return;

		// クライアント領域(実際のウィンドウの大きさ) → 描画解像度(UIの座標系)。
		// バックバッファは描画解像度で作られ、クライアント領域へ引き伸ばして
		// 表示されるので、比率を掛ければよい
		// (UIの当たり判定でも同じ変換をしている : UIInteraction::CalcCursorUIPos)
		const auto* _pGraphicsEngine = m_pServices->pMainEngine->RefGraphicsEngine();
		const float _renderW = static_cast<float>(_pGraphicsEngine->GetRenderWidth());
		const float _renderH = static_cast<float>(_pGraphicsEngine->GetRenderHeight());
		if (_renderW <= 0.0f || _renderH <= 0.0f) return;

		const auto* _pWindow = m_pServices->pMainEngine->GetNativeWindow();
		if (!_pWindow) return;

		const float _clientW = static_cast<float>(_pWindow->GetClientWidth());
		const float _clientH = static_cast<float>(_pWindow->GetClientHeight());
		if (_clientW <= 0.0f || _clientH <= 0.0f) return;

		const Math::Vector2 _renderPos = {
			m_clientPos.x * (_renderW / _clientW),
			m_clientPos.y * (_renderH / _clientH)
		};

		// ホットスポットをピボットに渡す。
		// SubmitUI はピボットの位置が指定座標に来るように描くので、
		// 画像の中の尖端がそのままカーソル位置に重なる
		// カーソルはどのUIよりも手前。
		// レイヤーは並べ替えのための値なので、大きくしても絵が消えることはない
		constexpr float CURSOR_LAYER = 10000.0f;

		Engine::Graphics::SpriteSubmitDesc _desc = {};
		_desc.texHandle = m_texRef;
		_desc.pixelPos = _renderPos;
		_desc.pixelSize = Math::Vector2(m_config.sizePixel, m_config.sizePixel);
		_desc.color = m_config.color;
		_desc.Zlayer = CURSOR_LAYER;
		_desc.pivot = m_config.hotspot;
		a_pDrawSubmitter->SubmitUI(_desc);
	}
}
