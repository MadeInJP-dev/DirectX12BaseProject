#include "FPSDisplay.h"

#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Option/OptionManager.h"		// ウィンドウ解像度(px)取得用

#include "Application/Object/UI/Decoration/DecorationDraw.h"

#include "../UserData/UserData.h"

namespace App::Game
{
	namespace
	{
		// 文字はゲームの UI と同じ経路で描く
		const Core::GUID FONT_GUID = Core::GUID("732a351f-cbc5-485b-a447-76ae733f15a1");	// LINESeedJP Bd

		constexpr float REFRESH_SEC = 0.5f;						// 数字を取り直す間隔(秒)。速いと読めない
		constexpr float FONT_PIXEL  = 24.0f;					// 文字の高さ(px)
		constexpr Math::Vector2 SIZE   = { 132.0f, 40.0f };		// 下敷きの大きさ(px)
		constexpr Math::Vector2 MARGIN = { 16.0f, 16.0f };		// 画面の右上の角からの離れ(px)

		// どの UI よりも手前、デバッグの表示(9000)とマウスカーソル(10000)よりは奥
		constexpr float LAYER = 8900.0f;
	}

	FPSDisplay::FPSDisplay()
	{}

	FPSDisplay::~FPSDisplay()
	{}

	//======================================================================================
	// 初期化
	//======================================================================================
	void FPSDisplay::Init(Engine::MainEngine& a_engine)
	{
		m_pEngine = &a_engine;

		// 下敷きの板 : 右上の角を基準に置く
		m_background.name      = "FPSDisplayBackground";
		m_background.pivot     = { 1.0f, 0.0f };
		m_background.pixelSize = SIZE;
		m_background.color     = { 0.0f, 0.0f, 0.0f, 0.5f };

		// 数字 : 板の中央
		m_text.name = "FPSDisplayText";
		m_text.SetType(Object::Decoration::EDecorationType::Text);
		m_text.pivot       = { 0.5f, 0.5f };
		m_text.offsetPos   = { -SIZE.x * 0.5f, SIZE.y * 0.5f };
		m_text.layerOffset = 1.0f;
		if (auto* _pText = m_text.RefText())
		{
			_pText->text          = "";
			_pText->fontGUID      = FONT_GUID;
			_pText->fontPixelSize = FONT_PIXEL;
			_pText->textAlign     = Object::Decoration::ETextAlign::Center;
		}
		Object::Decoration::RequestResources(m_text, a_engine.GetEngineServices().pResourceManager);
	}

	void FPSDisplay::Release()
	{
		if (auto* _pText = m_text.RefText()) _pText->fontRef = {};
	}

	//======================================================================================
	// 更新
	//======================================================================================
	void FPSDisplay::Update(const UserData* a_pUserData)
	{
		const bool _isVisible = a_pUserData && a_pUserData->GetGameSettings().isFPSVisible;

		// 出した瞬間に数字を取る(前に出していたときの数字を見せない)
		if (_isVisible && !m_isVisible) m_refreshTimer = 0.0f;
		m_isVisible = _isVisible;
		if (!m_isVisible) return;

		// 時間を止めても数字は動かしたいので、倍率を掛ける前の時間で数える
		m_refreshTimer -= m_pEngine->GetUnscaledDeltaTime();
		if (m_refreshTimer > 0.0f) return;
		m_refreshTimer = REFRESH_SEC;

		char _buf[32] = {};
		std::snprintf(_buf, sizeof(_buf), "FPS %u", m_pEngine->GetFPS());
		if (auto* _pText = m_text.RefText()) _pText->text = _buf;
	}

	//======================================================================================
	// 積む
	//======================================================================================
	void FPSDisplay::SubmitUI(Engine::Graphics::GraphicsEngine* a_pGraphicsEngine) const
	{
		if (!m_isVisible || !a_pGraphicsEngine) return;

		const auto& _services = m_pEngine->GetEngineServices();
		if (!_services.pResourceManager || !_services.pOptionManager) return;

		// UI の座標は描画解像度(px)。右上の角から離して置く
		const auto& _winOp = _services.pOptionManager->GetWindowOption();

		Object::Decoration::ParentTransform _parentTr = {};
		_parentTr.pixelPos = { static_cast<float>(_winOp.windowWidth) - MARGIN.x, MARGIN.y };
		_parentTr.layer    = LAYER;

		const Object::Decoration::ParentOption _parentOp = {};

		Object::Decoration::DrawDecoration(a_pGraphicsEngine, _services.pResourceManager, m_background, _parentTr, _parentOp);
		Object::Decoration::DrawDecoration(a_pGraphicsEngine, _services.pResourceManager, m_text, _parentTr, _parentOp);
	}
}
