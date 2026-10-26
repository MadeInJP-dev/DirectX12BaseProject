#include "InputConfig.h"

#include "InputManager.h"
#include "Engine/ECS/System/SystemContext.h"	// EngineServices
#include "Engine/EditorField/EditorField.h"

namespace
{
	// 感度の下限。0 にすると視点が一切動かなくなり、
	// 「設定をいじったら壊れた」と見えてしまうので触れないようにする
	constexpr float MIN_SENSITIVITY = 0.01f;
	constexpr float MAX_SENSITIVITY = 5.0f;

	// 既定値(ヘッダのメンバ初期化子と揃えること)
	constexpr float DEFAULT_SENSITIVITY = 0.5f;

	// 360度回すのに必要なマウスの移動距離(cm)を求める
	//
	//   1度あたりのカウント数 = 1 / 感度
	//   360度ぶんのカウント数 = 360 / 感度
	//   それをDPI(1インチあたりのカウント数)で割ってインチへ、2.54倍でcmへ
	float CalcCentiMeterPer360(float a_sensitivity, int a_dpi)
	{
		if (a_sensitivity <= 0.0f || a_dpi <= 0) return 0.0f;

		const float _countPer360 = 360.0f / a_sensitivity;
		return (_countPer360 / static_cast<float>(a_dpi)) * 2.54f;
	}
}

void Engine::Input::InputConfig::DrawEdit(const ECS::EngineServices& a_services)
{
	//======================================================================
	// カーソル
	//======================================================================
	Engine::EditorField::Header("Cursor");

	if (Engine::EditorField::Field("CursorLockToCenter", isCursorLockedToCenter))
	{
		if (a_services.pInputManager) a_services.pInputManager->SetCursorCentered(isCursorLockedToCenter);
	}
	Engine::EditorField::Tooltip("プレイ中だけ画面中央へ固定");

	//======================================================================
	// 視点感度
	//======================================================================
	Engine::EditorField::Header("Mouse Look");

	Engine::EditorField::Field("SensitivityX", lookSensitivityX, 0.005f, MIN_SENSITIVITY, MAX_SENSITIVITY, "%.3f deg/count");
	Engine::EditorField::Field("SensitivityY", lookSensitivityY, 0.005f, MIN_SENSITIVITY, MAX_SENSITIVITY, "%.3f deg/count");

	// DragFloat の min/max は入力欄への直接入力までは止めないので、ここで押さえる
	lookSensitivityX = std::clamp(lookSensitivityX, MIN_SENSITIVITY, MAX_SENSITIVITY);
	lookSensitivityY = std::clamp(lookSensitivityY, MIN_SENSITIVITY, MAX_SENSITIVITY);

	// 左右と上下を揃える(別々にしたい人のほうが少ないので、片方から合わせられるようにする)
	if (Engine::EditorField::Button("Y = X"))
	{
		lookSensitivityY = lookSensitivityX;
	}
	Engine::EditorField::SameLine();
	if (Engine::EditorField::Button("Reset"))
	{
		lookSensitivityX = DEFAULT_SENSITIVITY;
		lookSensitivityY = DEFAULT_SENSITIVITY;
		isInvertLookY    = false;
	}

	Engine::EditorField::Field("InvertY", isInvertLookY);
	Engine::EditorField::Tooltip("上下の反転");

	//======================================================================
	// 目安の表示
	//======================================================================
	// 感度の数値だけでは速さが想像できないので、
	// 「360度振り向くのにマウスを何cm動かすか」に直して見せる。
	// この欄は表示専用で、入力には影響しない。
	Engine::EditorField::Header("Reference");

	if (Engine::EditorField::Field("MouseDPI", mouseDpi, 50.0f, 100, 32000))
	{
		mouseDpi = std::clamp(mouseDpi, 100, 32000);
	}
	Engine::EditorField::Tooltip("表示の計算にだけ使う");

	Engine::EditorField::Value("360 turn", "%.1f cm (X) / %.1f cm (Y)", CalcCentiMeterPer360(lookSensitivityX, mouseDpi), CalcCentiMeterPer360(lookSensitivityY, mouseDpi));

	Engine::EditorField::HelpText("Windows側のポインター速度・加速は影響しない(生の入力を使用)");
}

void Engine::Input::InputConfig::Archive(Persistence::Archive& a_archive)
{
	a_archive.Field("isCursorLockedToCenter", isCursorLockedToCenter);

	// 旧データにキーが無い場合は既定値のまま読み飛ばされる
	a_archive.Field("lookSensitivityX", lookSensitivityX);
	a_archive.Field("lookSensitivityY", lookSensitivityY);
	a_archive.Field("isInvertLookY", isInvertLookY);
	a_archive.Field("mouseDpi", mouseDpi);
}
