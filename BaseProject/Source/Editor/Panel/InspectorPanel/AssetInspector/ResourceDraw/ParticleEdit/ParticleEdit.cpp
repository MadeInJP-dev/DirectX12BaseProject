#include "ParticleEdit.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

#include "../../AssetLink.h"

#include "../../../../../../Editor/Helper/EditorHelper.h"
#include "Engine/Resource/Data/Texture/IO/TextureIO.h"

namespace Editor::Inspector
{
	//-----------------------------------------------------------------------------------------
	// パーティクルアセットの編集・詳細表示
	//-----------------------------------------------------------------------------------------
	void ParticleEdit(
		const ECS::EngineServices& a_services,
		const Core::GUID& a_guid,
		Resource::ParticlesAsset* a_pParticles,
		EditorContext* a_pEditContext)
	{
		if (!a_pParticles) { return; }

		if (ImGui::Button("Save") && a_services.pAssetDatabase)
		{
			// ファイルパス取得 : 呼び出し側が選んでいるGUIDから引く
			auto _filePath = a_services.pAssetDatabase->GetFilePathFromGUID(a_guid);
			a_pParticles->Save(_filePath);
			ENGINE_LOG("%s: Save Particles", _filePath.c_str());
		}

		// パラメーター変更
		Engine::EditorField::Field("Name", a_pParticles->RefName());
		Engine::EditorField::Text("%s", a_guid.String().c_str());

		Engine::EditorField::Line();

		ImGui::PushID(1);
		Engine::EditorField::Header("InitialSpeed");
		Engine::EditorField::Field("Min", a_pParticles->RefInitalSpeedMin(), 0.1f, 0.0f);
		Engine::EditorField::Field("Max", a_pParticles->RefInitalSpeedMax(), 0.1f, 0.0f);
		ImGui::PopID();

		Engine::EditorField::Line();

		// 下限を 0 にしない : 負の値で浮き上がらせたいことがある(煙・炎)
		Engine::EditorField::Field("GravityPow", a_pParticles->RefGravityPow(), 0.05f);
		Engine::EditorField::Tooltip("1 で普通に落ちる / 0 で無重力 / 負で浮き上がる");

		// ---- 空気抵抗 ----
		// 勢いよく飛び出して失速する動き。爆発の破片や煙はこれが無いと
		// 最後まで等速で飛んでいってしまう
		Engine::EditorField::Field("Drag (/s)", a_pParticles->RefDrag(), 0.05f, 0.0f);
		if (a_pParticles->GetDrag() <= 0.0f)
		{
			Engine::EditorField::HelpText("0 : 減速しない(等速で飛び続ける)");
		}
		else
		{
			Engine::EditorField::HelpText("大きいほど早く失速する(爆発の破片なら 2〜5 が目安)");
		}

		Engine::EditorField::Line();

		ImGui::PushID(2);
		Engine::EditorField::Header("LifeTime");
		Engine::EditorField::Field("Min", a_pParticles->RefLifeTimeMin(), 0.1f, 0.0f);
		Engine::EditorField::Field("Max", a_pParticles->RefLifeTimeMax(), 0.1f, 0.0f);
		ImGui::PopID();

		Engine::EditorField::Line();

		Engine::EditorField::Field("Initial Capacity", a_pParticles->RefCapacity(), 1, 0);
		Engine::EditorField::Tooltip("最初に用意しておく粒の数(目安)。足りなくなったら自動で伸びる。"
			"大きく伸びたプールは、使われなくなってしばらくするとこの数まで縮む");
		Engine::EditorField::Field("EmissionRate", a_pParticles->RefEmissionRate(), 1, 0);

		Engine::EditorField::Header("Over Lifetime");
		Engine::EditorField::HelpText("寿命のどこまで進んだかで、サイズと色を動かす");

		// ---- サイズの変化 ----
		Engine::EditorField::Field("EndSizeScale", a_pParticles->RefEndSizeScale(), 0.05f, 0.0f);
		Engine::EditorField::Tooltip("寿命の終わりでのサイズ倍率。煙は 1 より大きく、火花は小さく");
		Engine::EditorField::Field("SizeCurvePow", a_pParticles->RefSizeCurvePow(), 0.01f, 0.01f, 8.0f);
		Engine::EditorField::Tooltip("サイズの変化の曲がり具合。1 で一定、1 より小さいと頭で一気に膨らむ(爆風・煙は 0.3〜0.6)");

		// ---- 色の変化 ----
		// RGB は 1 を超えてよい。超えたぶんがブルームのしきい値を抜けて光る
		EditorField::Field("StartColor", a_pParticles->RefStartColor());
		EditorField::Field("MidColor", a_pParticles->RefMidColor());
		Engine::EditorField::Field("MidColorTime", a_pParticles->RefMidColorTime(), 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("途中の色になる進み具合。0 なら途中の色を使わない(開始→終わりの2色)");
		EditorField::Field("EndColor", a_pParticles->RefEndColor());
		Engine::EditorField::Tooltip("RGB は 1 を超えてよい(超えたぶんが光る)。爆発は白→橙→暗い煙");

		// ---- フェード ----
		// 寿命に対する割合で持つので、粒ごとに寿命がばらついても見え方が揃う
		Engine::EditorField::Field("FadeIn (ratio)", a_pParticles->RefFadeInRatio(), 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("FadeOut (ratio)", a_pParticles->RefFadeOutRatio(), 0.01f, 0.0f, 1.0f);
		if (a_pParticles->GetFadeInRatio() + a_pParticles->GetFadeOutRatio() > 1.0f)
		{
			Engine::EditorField::HelpText("合計が 1 を超えています(不透明になりきる前に消え始めます)");
		}

		// ---- どの座標系で回すか ----
		// ワールドのままだと、発生源が横へ動いた瞬間に出した粒だけ置き去りになる。
		// 噴射のように発生源へくっついてほしいものは Local
		Engine::EditorField::Header("Simulation");
		EditorField::Field("SimulationSpace", a_pParticles->RefSimulationSpace());
		if (a_pParticles->IsLocalSpace())
		{
			Engine::EditorField::HelpText("発生源にくっついて動く(ブースターの噴射など)");
			Engine::EditorField::HelpText("※ 重力はワールドの下向きのまま掛かる");
		}
		else
		{
			Engine::EditorField::HelpText("出したその場に残る(煙・爆発・弾の軌跡など)");
		}
		Engine::EditorField::HelpText("※ 既定値。エフェクトのパーツの SimulationSpace で上書きできる");

		// ---- 色の重ね方 ----
		// 加算は光り物、半透明は煙や破片。
		// 加算のまま煙を出すと背景ごと明るくなってしまう
		Engine::EditorField::Header("Blend");
		EditorField::Field("BlendMode", a_pParticles->RefBlendMode());
		if (a_pParticles->GetBlendMode() == Graphics::Particle::EParticleBlendMode::Additive)
		{
			Engine::EditorField::HelpText("重ねるほど明るくなる。火花・炎・爆発の芯向き");
		}
		else
		{
			Engine::EditorField::HelpText("背景を明るくしない。煙・破片向き");
		}
		Engine::EditorField::HelpText("※ 加算・半透明とも、全アセットの粒をカメラから遠い順に並べて描く");

		// 距離が同じ粒どうしで描く順。小さいほど先(下)に描かれ、大きいものが上に乗る
		Engine::EditorField::Field("SortOrder", a_pParticles->RefSortOrder(), 1.0f);
		Engine::EditorField::Tooltip("距離が同じ粒どうしの描く順。小さいほど先(下)。同じなら名前順");

		// 奥の物との境目をぼかす幅。板が地面へ刺さったところで絵がまっすぐ切れるのを隠す
		Engine::EditorField::Field("SoftDepth (m)", a_pParticles->RefSoftDepth(), 0.05f, 0.0f, 100.0f);
		Engine::EditorField::Tooltip("奥の物(地面・壁)との距離がこの幅より近いところほど薄くする。0 でぼかさない");

		Engine::EditorField::Line();

		// ---- 板ポリの向き ----
		// 進行方向に画像を回すかどうか。Billboard 以外のとき Stretch が効く
		EditorField::Field("Orientation", a_pParticles->RefOrientation());

		// 向きごとの説明と、効く項目だけを出す
		bool _isStretch = false;	// 縦(画像の上)へ伸ばせるか
		bool _isRotate = false;		// 板を面の中で回せるか
		switch (a_pParticles->GetOrientation())
		{
		case Graphics::Particle::EParticleOrientation::Billboard:
			Engine::EditorField::HelpText("Always faces camera (texture up = screen up)");
			_isRotate = true;
			break;
		case Graphics::Particle::EParticleOrientation::VelocityBillboard:
		case Graphics::Particle::EParticleOrientation::VelocityAxis:
			Engine::EditorField::HelpText("Texture up (V=0) points along velocity");
			_isStretch = true;
			break;
		case Graphics::Particle::EParticleOrientation::EmitterAxis:
			Engine::EditorField::HelpText("Texture up (V=0) points along emit direction (emitter +Z)");
			_isStretch = true;
			_isRotate = true;
			break;
		case Graphics::Particle::EParticleOrientation::EmitterFacing:
			Engine::EditorField::HelpText("Faces emit direction (texture up = emitter +Y)");
			_isRotate = true;
			break;
		default:
			break;
		}

		if (_isStretch)
		{
			Engine::EditorField::Field("Stretch", a_pParticles->RefStretch(), 0.05f, 0.01f);
		}

		// 板を面の中で回す : 初期角は粒ごとの乱数、回転速度も乱数(負で逆回り)
		if (_isRotate)
		{
			Engine::EditorField::Field("RotationMin (deg)", a_pParticles->RefRotationMin(), 1.0f);
			Engine::EditorField::Field("RotationMax (deg)", a_pParticles->RefRotationMax(), 1.0f);
			Engine::EditorField::Field("AngularVelocityMin (deg/s)", a_pParticles->RefAngularVelocityMin(), 1.0f);
			Engine::EditorField::Field("AngularVelocityMax (deg/s)", a_pParticles->RefAngularVelocityMax(), 1.0f);
		}

		Engine::EditorField::Line();

		// ---- フリップブック ----
		// 絵をコマに割って送る。1x1 ならコマ送りしない
		Engine::EditorField::Header("Flipbook");
		Engine::EditorField::Field("Cols", a_pParticles->RefFlipbookCols(), 1, 1, 64);
		Engine::EditorField::Field("Rows", a_pParticles->RefFlipbookRows(), 1, 1, 64);
		EditorField::Field("FlipbookMode", a_pParticles->RefFlipbookMode());
		switch (a_pParticles->GetFlipbookMode())
		{
		case Graphics::Particle::EParticleFlipbookMode::OverLife:
			Engine::EditorField::HelpText("寿命に沿って左上のコマから右下のコマまで送る");
			Engine::EditorField::Field("Blend", a_pParticles->RefIsFlipbookBlend());
			Engine::EditorField::Tooltip("隣のコマと混ぜてなめらかに送る");
			break;
		case Graphics::Particle::EParticleFlipbookMode::Random:
			Engine::EditorField::HelpText("粒ごとにどれか1コマを選ぶ(破片の形を散らす)");
			break;
		default:
			Engine::EditorField::HelpText("1コマ目だけを出す");
			break;
		}

		// ---- 絵のチャンネルの読み方 ----
		Engine::EditorField::Header("Texture Channels");
		EditorField::Field("TexMode", a_pParticles->RefTexMode());
		if (a_pParticles->GetTexMode() == Graphics::Particle::EParticleTexMode::FireSmoke)
		{
			Engine::EditorField::HelpText("R = 煙の明暗 / G = 温度 / A = 濃さ");
			Engine::EditorField::HelpText("色 = 寿命の色 × R + 発光色 × 熱いところ");
			EditorField::Field("EmissiveColor", a_pParticles->RefEmissiveColor());
			Engine::EditorField::Tooltip("熱いところの発光色。1 を超えてよい(超えたぶんが光る)");
			Engine::EditorField::Field("EmissiveTime", a_pParticles->RefEmissiveTime(), 0.01f, 0.0f, 1.0f);
			Engine::EditorField::Tooltip("寿命のこの割合までに、火が外側から冷えて消える。0 で光らない");
		}
		else
		{
			Engine::EditorField::HelpText("RGBA をそのまま色として掛ける");
		}

		Engine::EditorField::Line();

		// 現在選択されているテクスチャ
		const auto* _pTex = a_services.pResourceManager->Ref(a_pParticles->GetTexHandle());

		Engine::EditorField::Line();

		// テクスチャ選択コンボボックス
		// 反映は専用のロード関数を通すので、選択だけを共通ヘルパーに任せる
		Core::GUID _selectedGUID = {};
		if (EditorField::AssetPicker(
			a_services,
			"SelectTexture",
			"Texture",
			a_pParticles->GetTexGUID(),
			_selectedGUID))
		{
			// テクスチャのハンドル取得
			// ロードされていなかったら止まる
			const auto _context = Resource::MakeManagerOnlyContext(a_services.pResourceManager, a_services.pAssetDatabase);
			a_pParticles->SetTexture(_selectedGUID, Resource::TextureIO::LoadTexture(_selectedGUID, TexColor::WHITE, &_context));
		}

		// 今指しているテクスチャ。押せばテクスチャのインスペクターへ飛べる
		DrawAssetLink(a_pEditContext, "Texture :", a_pParticles->GetTexGUID());

		// テクスチャの画像を表示
		if (_pTex)
		{
			auto _gpuHandle = EditorHelper::GetImGuiTexHandle(_pTex->GetImGuiSRV());
			EditorHelper::DrawSRVView(_gpuHandle, static_cast<float>(_pTex->GetDesc().Width), static_cast<float>(_pTex->GetDesc().Height));
		}
	}
}
