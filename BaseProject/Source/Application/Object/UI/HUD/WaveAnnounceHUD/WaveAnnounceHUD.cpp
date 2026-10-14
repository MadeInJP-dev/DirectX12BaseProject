#include "WaveAnnounceHUD.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/MainEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Audio/AudioManager.h"
#include "Engine/Option/OptionManager.h"			// ウィンドウ解像度(px)取得用

#include "Engine/ECS/World/World.h"

#include "Application/InstanceResource/WaveAnnounceResource.h"

namespace App::Object
{
	namespace
	{
		// 新規追加時の既定表示サイズ(px)
		constexpr float DEFAULT_LABEL_WIDTH  = 480.0f;
		constexpr float DEFAULT_LABEL_HEIGHT = 96.0f;

		// 新規追加時に画面のどのあたりへ置くか(高さの割合)。
		// レティクルと重ならないよう、真ん中より少し上へ出す
		constexpr float DEFAULT_SCREEN_Y_RATE = 0.28f;
	}

	void WaveAnnounceHUD::PostDeserialize(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices || !a_context.pServices->pOptionManager) return;

		// サイズが0のままだと何も見えないので、既定サイズと表示位置を入れておく。
		// 保存値を読み終えた後に見るので、シーンに入っている値は潰さない
		if (m_anchor.pixelSize.x <= 0.0f || m_anchor.pixelSize.y <= 0.0f)
		{
			m_anchor.pixelSize = { DEFAULT_LABEL_WIDTH, DEFAULT_LABEL_HEIGHT };
			m_anchor.editSize  = m_anchor.pixelSize;

			const auto& _winOp = a_context.pServices->pOptionManager->GetWindowOption();
			m_anchor.pixelPos = {
				static_cast<float>(_winOp.windowWidth) * 0.5f,
				static_cast<float>(_winOp.windowHeight) * DEFAULT_SCREEN_Y_RATE
			};
		}
	}

	//======================================================================================
	// リソースの要求
	//======================================================================================
	void WaveAnnounceHUD::Awake(Engine::GameObject::ObjectContext& a_context)
	{
		// 飾り(文字・枠)は差し替え前提なので既定の絵は持たない
		RequestDecorationResources(a_context);

		CreateSound(a_context);
	}

	void WaveAnnounceHUD::Release(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::Release(a_context);

		// サウンドインスタンスのプールはアプリ寿命なので、借りた側が必ず返す
		if (a_context.pServices && a_context.pServices->pAudioManager)
		{
			a_context.pServices->pAudioManager->ReleaseSoundInstance(m_soundHandle);
		}
		m_soundHandle = {};
	}

	void WaveAnnounceHUD::CreateSound(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices || !a_context.pServices->pAudioManager) return;

		auto* _pAudioManager = a_context.pServices->pAudioManager;

		// 借りていたものは先に返す(差し替えのたびに溜まらないように)
		_pAudioManager->ReleaseSoundInstance(m_soundHandle);
		m_soundHandle = {};

		if (m_soundGUID == Core::DEFAULT_GUID) return;

		// 画面に出す音なので 2D で発行する(定位を付けない)
		m_soundHandle = _pAudioManager->CreateSoundInstance(m_soundGUID, false);

		if (auto* _pInstance = _pAudioManager->RefInstance(m_soundHandle))
		{
			_pInstance->SetVolume(m_volume);
		}
	}

	//======================================================================================
	// 出す文字列
	//--------------------------------------------------------------------------------------
	// データ上のウェーブ番号は0始まりだが、出すときは1始まりにする
	// (「WAVE 0」から始まると、何番目なのかが伝わらないため)
	//======================================================================================
	std::string WaveAnnounceHUD::MakeLabel(int a_waveIndex, int a_waveCount) const
	{
		std::string _label = m_prefix + std::to_string(a_waveIndex + 1);

		if (m_isShowTotal && a_waveCount > 0)
		{
			_label += m_separator + std::to_string(a_waveCount);
		}

		return _label;
	}

	//======================================================================================
	// Text の飾りへ文字列を入れる
	//--------------------------------------------------------------------------------------
	// 毎フレームの差し替え(DrawOverride)ではなく、飾りの値そのものを書き換えている。
	// 文字が変わるのはウェーブが出た瞬間だけなので、次のウェーブまで残ってよい。
	// DrawOverride には文字の差し替えが無く、増やすと全UIが持つ構造体が太る
	//======================================================================================
	void WaveAnnounceHUD::ApplyLabel(const std::string& a_label)
	{
		for (Decoration::Decoration& _decoration : RefDecorations())
		{
			if (Decoration::TextData* _pText = _decoration.RefText()) _pText->text = a_label;
		}
	}

	void WaveAnnounceHUD::OnWaveSpawned(
		Engine::GameObject::ObjectContext& a_context, int a_waveIndex, int a_waveCount)
	{
		m_lastWaveIndex = a_waveIndex;

		ApplyLabel(MakeLabel(a_waveIndex, a_waveCount));

		// 出ている最中に次のウェーブが出たら、そこから出し直す
		m_remainTime = m_showTime;

		if (!a_context.pServices || !a_context.pServices->pAudioManager) return;

		if (auto* _pInstance = a_context.pServices->pAudioManager->RefInstance(m_soundHandle))
		{
			_pInstance->SetVolume(m_volume);
			_pInstance->Play(false);
		}
	}

	void WaveAnnounceHUD::Update(Engine::GameObject::ObjectContext& a_context)
	{
		// 飾りのアニメーションを進める
		UIBase::Update(a_context);

		// 表示時間を進める
		if (m_remainTime > 0.0f) m_remainTime = std::max(m_remainTime - a_context.dt, 0.0f);

		auto* _pWorld = a_context.pWorld;
		if (!_pWorld) return;
		if (!_pWorld->HasResource<InstanceResource::WaveAnnounceResource>()) return;

		const InstanceResource::WaveAnnounceResource& _announce = _pWorld->GetResource<InstanceResource::WaveAnnounceResource>();

		// まだ一度も出ていない
		if (_announce.waveIndex < 0) return;

		// 通し番号が変わっていなければ、同じウェーブをもう一度出さない。
		// 積む側とこちらの実行順は決まらないので、
		// 「まだ読んでいない回があるか」を番号の差で見る
		if (_announce.serial == m_lastSerial) return;

		m_lastSerial = _announce.serial;

		OnWaveSpawned(a_context, _announce.waveIndex, _announce.waveCount);
	}

	void WaveAnnounceHUD::Draw(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_remainTime <= 0.0f) return;

		// 残り時間の割合(1 → 0)。出た瞬間が 1
		const float _rate = (m_showTime > 1e-4f)
			? std::clamp(m_remainTime / m_showTime, 0.0f, 1.0f)
			: 1.0f;

		Decoration::DrawOverride _override = {};

		// 出た瞬間だけ少し大きく見せる(punchScale → 等倍へ戻る)
		_override.scale = 1.0f + (m_punchScale - 1.0f) * _rate;

		// 消えぎわに薄くする : 掛ける色なので、飾りごとの色はそのまま残る
		if (m_isFadeOut) _override.tint.a = _rate;

		DrawDecorations(a_context, _override);
	}

	void WaveAnnounceHUD::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context)
	{
		// テクスチャ・色・サイズなどの共通ぶん
		UIBase::Archive(a_ar, a_context);

		// ここから下は WaveAnnounceHUD のぶん。基底(UIBase)とは区切りを分けてあるので、
		// どちらに足しても互いの読み出しはずれない。足すときはこの区切りの末尾へ
		Engine::Persistence::ArchiveSection _section(a_ar, "WaveAnnounceHUD");

		a_ar.GUIDField("SoundGUID", m_soundGUID);
		a_ar.Field("Volume", m_volume);
		a_ar.Field("ShowTime", m_showTime);
		a_ar.Field("IsFadeOut", m_isFadeOut);
		a_ar.Field("PunchScale", m_punchScale);

		a_ar.StringField("Prefix", m_prefix);
		a_ar.Field("IsShowTotal", m_isShowTotal);
		a_ar.StringField("Separator", m_separator);

		// 読み込み時は復元したGUIDでサウンドを取り直す
		if (a_ar.IsLoading())
		{
			CreateSound(a_context);
		}
	}

	void WaveAnnounceHUD::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::DrawInspector(a_context);

		Engine::EditorField::Header("WaveAnnounce");

		// 合図の音(アセットDBの Sound 一覧から選ぶ)
		if (Engine::EditorField::AssetField(
			*a_context.pServices,
			"Wave Sound",
			"Sound",
			m_soundGUID))
		{
			CreateSound(a_context);
		}

		if (Engine::EditorField::Field("Volume", m_volume, 0.01f, 0.0f, 1.0f))
		{
			// 鳴らしながら調整できるよう、発行済みインスタンスへ即時反映する
			if (a_context.pServices && a_context.pServices->pAudioManager)
			{
				if (auto* _pInstance = a_context.pServices->pAudioManager->RefInstance(m_soundHandle))
				{
					_pInstance->SetVolume(m_volume);
				}
			}
		}

		Engine::EditorField::Line();
		Engine::EditorField::Field("ShowTime", m_showTime, 0.1f, 0.0f, 10.0f);
		Engine::EditorField::Field("FadeOut", m_isFadeOut);
		Engine::EditorField::Field("PunchScale", m_punchScale, 0.01f, 0.1f, 4.0f);

		Engine::EditorField::Line();

		// 文字の組み立て
		{
			char _buf[64] = {};
			std::snprintf(_buf, sizeof(_buf), "%s", m_prefix.c_str());
			if (Engine::EditorField::Field("Prefix", _buf, sizeof(_buf))) m_prefix = _buf;
		}
		Engine::EditorField::Field("ShowTotal", m_isShowTotal);
		if (m_isShowTotal)
		{
			char _buf[16] = {};
			std::snprintf(_buf, sizeof(_buf), "%s", m_separator.c_str());
			if (Engine::EditorField::Field("Separator", _buf, sizeof(_buf))) m_separator = _buf;
		}

		// 確認用に出してみる
		if (Engine::EditorField::Button("Test"))
		{
			OnWaveSpawned(a_context, (m_lastWaveIndex >= 0) ? m_lastWaveIndex : 0, 0);
		}

		Engine::EditorField::Line();
		Engine::EditorField::Value("LastWave", "%d", m_lastWaveIndex + 1);
		Engine::EditorField::Value("Remain", "%.2f", m_remainTime);
		Engine::EditorField::Tooltip("SceneSequence がウェーブを出したフレームに反応します\n文字は Text の飾りへ入るので、飾りを1つ足してフォントを選んでください");
	}
}
