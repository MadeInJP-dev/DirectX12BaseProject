#pragma once

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/EffectAsset/EffectAsset.h"
#include "Engine/EditorField/EditorField.h"
#include "Engine/EditorField/EditorField.inl"

namespace App::Component
{
	//==========================================================================================
	// BoosterEffectComponent
	//
	// ブースター(スラスター)の噴射エフェクト専用の設定。
	// 噴射のエンティティに EffectAssetComponent と一緒に付ける。
	//
	// ・持つのは「置き方」と「吹かした瞬間の膨らみ」だけ。
	//   何を出すか(粒の絵・寿命・色)は EffectAsset の持ち物で、こちらは触らない。
	//
	// ・置き方をここが持つ理由
	//     エフェクトアセットは GUID 単位で全員に共有される。ブースターは機体の
	//     左右・腕・脚で取り付け位置も向きも違うので、そこをアセットに書くと
	//     ブースターの数だけアセットを増やすことになり、絵を1つ直すのに
	//     全部を開いて回る羽目になる。
	//     「アセット = 中身 / コンポーネント = 置き方」で分けてある。
	//     実際の反映は BoosterEffectSystem が EffectAssetComponent の
	//     上書き欄(overridePosOffset / overrideEmitDir / effectScale)へ書き込む。
	//
	// ・吹かした瞬間の膨らみ
	//     点火(isPlay の立ち上がり)で burstScale まで一気に太らせ、
	//     burstTime かけて baseScale へ戻す。
	//     噴射が出っぱなしの一定量だと、吹かし始めの「ドンッ」という手応えが出ない。
	//
	// ・ブーストダッシュ(Shift)の見せ方
	//     ジェットは「出ているか」しか変わらないので、歩いている時と
	//     一気に加速した時が同じ絵になってしまう。そこで2つ足してある。
	//       (1) ダッシュしている間はジェットを boostScale 倍に太らせる(持続)
	//       (2) 踏み込んだ瞬間にスパークのエフェクトを1回だけ出す(瞬間)
	//     (1) はジェットそのものの大きさなのでここが倍率を持ち、
	//     (2) は絵がまったく別物なので、ジェットのアセットとは分けて
	//     sparkEffectGUID に別の EffectAsset を持たせる。
	//     同じ瞬間に、機体を包むほどの炎(blazeEffectGUID)も重ねて出せる。
	//     どちらを動かすかは BoosterEffectSystem が isBoosting を見て決める
	//     (isBoosting を書くのは ThrusterEffectSystem)。
	//
	// ・チャージダッシュ(Space長押し)の見せ方
	//     溜めと撃ち出しで動かす軸を分けてある。
	//       溜め   : 溜まり具合ぶんジェットを chargeScale まで太らせる(だんだん)
	//       撃ち出し: 束の長さを dashLengthScale 倍にする(粒の初速に掛かる)
	//     どちらも太さでやると「溜まりきった」のか「もう出た」のかが読めない。
	//     溜め具合と撃ち出し中かを配るのは ThrusterEffectSystem で、
	//     元を持っているのは機体側の ChargeDashComponent。
	//
	// ・以前はパーティクルコンポーネント(ParticlesComponent)を直に付けていたが、
	//   あちらは汎用なので、ブースター1つを調整するのに
	//   発生量・寿命・絵といった「アセットに書くべきもの」まで並んでしまっていた。
	//==========================================================================================
	struct BoosterEffectComponent
	{
		// ---- 取り付け(設定値) ----
		// どちらもこのエンティティの行列を基準にしたローカル値
		Math::Vector3 posOffset = { 0.0f, 0.0f, 0.0f };		// 噴射口の位置
		Math::Vector3 emitDir = { 0.0f, 0.0f, -1.0f };		// 噴射する向き(正規化はシステム側で行う)

		// ---- 吹かした瞬間の膨らみ(設定値) ----
		float baseScale = 1.0f;		// 通常時のスケール倍率
		float burstScale = 1.8f;	// 点火した瞬間のスケール倍率(baseScale より小さくすると縮んでから戻る)
		float burstTime = 0.18f;	// baseScale へ戻るまでの秒数。0 なら膨らませない

		// ---- ブーストダッシュ中の太らせ(設定値) ----
		// 上の膨らみに掛ける倍率。1 なら通常移動と同じ太さのまま
		float boostScale = 1.7f;
		// 倍率の行き来にかける秒数。0 なら即座に切り替わる。
		// 入り切りをそのまま出すとジェットが1フレームで跳ねるので、少しだけ均す
		float boostBlendTime = 0.08f;

		// ---- チャージダッシュ(設定値) ----
		// 溜めている間は少しずつ太らせ、撃ち出した瞬間に束を前へ伸ばす。
		//
		// 溜めを太さで、撃ち出しを長さで見せているのは、2つが同時に起きないため。
		// どちらも太さでやると「溜まりきった」のか「もう出た」のかが読み取れず、
		// 撃ち出しの側を太さでやると、ただ膨らむだけで前へ進む感じが出ない
		float chargeScale = 1.6f;		// 溜まりきったときのスケール倍率(1 なら太らない)
		float dashLengthScale = 2.0f;	// ダッシュ中の束の長さの倍率(粒の初速に掛かる)
		// 長さの行き来にかける秒数。0 なら即座に切り替わる。
		// 撃ち出しは一瞬で伸びてほしいので、太さの boostBlendTime より短めが合う
		float dashLengthBlendTime = 0.05f;

		// ---- 踏み込んだ瞬間のスパーク(設定値) ----
		// ジェットとは別のエフェクトを噴射口へ1回だけ出す。
		// 未設定なら何も出ない(ジェットの太らせだけが効く)
		Core::GUID sparkEffectGUID = Core::DEFAULT_GUID;
		Engine::Handle<Engine::Resource::EffectAsset> sparkHandle = {};	// EffectFixupSystem が解決する
		float sparkScale = 1.0f;	// スパークの大きさ倍率(アセットは共有なので個体差はここで付ける)

		// ---- 踏み込んだ瞬間の炎の噴き上がり(設定値) ----
		// スパークと同じ瞬間に、機体を包むほどの炎を噴射口から1回だけ出す。
		// スパーク(弾ける光)とは絵も大きさも別物なので、アセットを分けて重ねる。
		// 未設定なら何も出ない(スパークだけが出る)
		Core::GUID blazeEffectGUID = Core::DEFAULT_GUID;
		Engine::Handle<Engine::Resource::EffectAsset> blazeHandle = {};	// EffectFixupSystem が解決する
		float blazeScale = 1.0f;	// 炎の大きさ倍率

		// ---- ランタイム(保存しない) ----
		float burstTimer = 0.0f;	// 戻るまでの残り時間
		bool  wasPlaying = false;	// 点火の立ち上がりを見るための前フレームの状態

		bool  isBoosting = false;	// ブーストダッシュ中か(ThrusterEffectSystem が毎フレーム書く)
		bool  wasBoosting = false;	// 踏み込みの立ち上がりを見るための前フレームの状態
		float boostBlend = 0.0f;	// 0 = 通常 / 1 = ブースト中。boostBlendTime で行き来する

		float chargeRate = 0.0f;	// チャージの溜まり具合 0〜1(ThrusterEffectSystem が毎フレーム書く)
		bool  isChargeDashing = false;	// チャージダッシュ中か(同上)
		float dashLengthBlend = 0.0f;	// 0 = 通常 / 1 = ダッシュ中。dashLengthBlendTime で行き来する
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::BoosterEffectComponent>
{
	//----------------------------------------------------------------------------------
	// 借りているリソースを返す
	//
	// コンポーネントはデストラクタが走らないので、参照を返すのはここの仕事。
	// ECS がエンティティを消すとき・コンポーネントを外すとき・
	// PostDeserialize へ入り直すとき(fixup が取り直す)に必ず呼ぶ。
	//----------------------------------------------------------------------------------
	static void Release(void* a_pData, const Engine::ECS::EngineServices& a_services)
	{
		App::Component::BoosterEffectComponent& _comp = Engine::EditorField::RefValue<App::Component::BoosterEffectComponent>(a_pData);
		auto& _resourceManager = *a_services.pResourceManager;

		_resourceManager.ReleaseHandle(_comp.sparkHandle);
		_resourceManager.ReleaseHandle(_comp.blazeHandle);
	}

	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::BoosterEffectComponent& _comp = Engine::EditorField::RefValue<App::Component::BoosterEffectComponent>(a_pData);

		a_ar.Field("posOffset", _comp.posOffset);
		a_ar.Field("emitDir", _comp.emitDir);

		a_ar.Field("baseScale", _comp.baseScale);
		a_ar.Field("burstScale", _comp.burstScale);
		a_ar.Field("burstTime", _comp.burstTime);

		a_ar.Field("boostScale", _comp.boostScale);
		a_ar.Field("boostBlendTime", _comp.boostBlendTime);

		a_ar.Field("chargeScale", _comp.chargeScale);
		a_ar.Field("dashLengthScale", _comp.dashLengthScale);
		a_ar.Field("dashLengthBlendTime", _comp.dashLengthBlendTime);

		a_ar.Field("sparkEffectGUID", _comp.sparkEffectGUID);
		a_ar.Field("sparkScale", _comp.sparkScale);

		// 後から足したもの : 古いバイナリには無いので読まない(既定の「未設定」のまま = 炎は出ない)
		if (!a_ar.IsLegacyLayout())
		{
			a_ar.Field("blazeEffectGUID", _comp.blazeEffectGUID);
			a_ar.Field("blazeScale", _comp.blazeScale);
		}
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::BoosterEffectComponent& _comp = Engine::EditorField::RefValue<App::Component::BoosterEffectComponent>(a_context.pData);

		Engine::EditorField::Header("Mount");
		Engine::EditorField::HelpText("このエンティティの行列基準。エフェクトの置き方だけを決める");
		Engine::EditorField::Field("PosOffset", _comp.posOffset, 0.01f);
		Engine::EditorField::Field("EmitDir (local)", _comp.emitDir, 0.01f);

		Engine::EditorField::Header("Burst");
		Engine::EditorField::HelpText("点火した瞬間だけ大きく見せて、時間で元の大きさへ戻す");
		Engine::EditorField::Field("BaseScale", _comp.baseScale, 0.01f, 0.0f);
		Engine::EditorField::Field("BurstScale", _comp.burstScale, 0.01f, 0.0f);
		Engine::EditorField::Field("BurstTime (s)", _comp.burstTime, 0.01f, 0.0f);
		if (_comp.burstTime <= 0.0f)
		{
			Engine::EditorField::HelpText("0 : 膨らませない(常に BaseScale)");
		}

		Engine::EditorField::Header("Boost Dash");
		Engine::EditorField::HelpText("ブースト中だけジェットを太らせる(上の大きさに掛かる)");
		Engine::EditorField::Field("BoostScale", _comp.boostScale, 0.01f, 0.0f);
		Engine::EditorField::Field("BoostBlendTime (s)", _comp.boostBlendTime, 0.01f, 0.0f);
		if (_comp.boostScale <= 1.0f)
		{
			Engine::EditorField::HelpText("1 以下 : ブーストしても太らない");
		}

		Engine::EditorField::Header("Charge Dash");
		Engine::EditorField::HelpText("溜めている間は太らせ、撃ち出している間は束を前へ伸ばす");
		Engine::EditorField::Field("ChargeScale", _comp.chargeScale, 0.01f, 0.0f);
		if (_comp.chargeScale <= 1.0f)
		{
			Engine::EditorField::HelpText("1 以下 : 溜めても太らない");
		}
		Engine::EditorField::Field("DashLengthScale", _comp.dashLengthScale, 0.01f, 0.0f);
		Engine::EditorField::Field("DashLengthBlendTime (s)", _comp.dashLengthBlendTime, 0.01f, 0.0f);
		if (_comp.dashLengthScale <= 1.0f)
		{
			Engine::EditorField::HelpText("1 以下 : 撃ち出しても伸びない");
		}

		Engine::EditorField::Header("Boost Spark");
		Engine::EditorField::HelpText("踏み込んだ瞬間に噴射口へ1回だけ出す。ジェットとは別のアセット");
		Engine::EditorField::AssetField<Engine::Resource::EffectAsset>(
			*a_context.pWorld->RefEngineServices(),
			"Spark Effect",
			"EffectAsset",
			_comp.sparkEffectGUID,
			_comp.sparkHandle);
		Engine::EditorField::Field("SparkScale", _comp.sparkScale, 0.01f, 0.0f);
		if (_comp.sparkEffectGUID == Core::DEFAULT_GUID)
		{
			Engine::EditorField::HelpText("(未設定 : ダッシュしても何も出ない)");
		}

		Engine::EditorField::Header("Boost Blaze");
		Engine::EditorField::HelpText("スパークと同じ瞬間に、機体を包むほどの炎を1回だけ出す");
		Engine::EditorField::AssetField<Engine::Resource::EffectAsset>(
			*a_context.pWorld->RefEngineServices(),
			"Blaze Effect",
			"EffectAsset",
			_comp.blazeEffectGUID,
			_comp.blazeHandle);
		Engine::EditorField::Field("BlazeScale", _comp.blazeScale, 0.01f, 0.0f);
		if (_comp.blazeEffectGUID == Core::DEFAULT_GUID)
		{
			Engine::EditorField::HelpText("(未設定 : 炎は出ない)");
		}

		// ランタイムは表示のみ
		Engine::EditorField::Header("Runtime");
		Engine::EditorField::Value("BurstTimer", "%.3f", _comp.burstTimer);
		Engine::EditorField::Value("Playing", "%s", _comp.wasPlaying ? "true" : "false");
		Engine::EditorField::Value("Boosting", "%s", _comp.isBoosting ? "true" : "false");
		Engine::EditorField::Value("BoostBlend", "%.3f", _comp.boostBlend);
		Engine::EditorField::Value("ChargeRate", "%.3f", _comp.chargeRate);
		Engine::EditorField::Value("ChargeDash", "%s", _comp.isChargeDashing ? "true" : "false");
		Engine::EditorField::Value("DashBlend", "%.3f", _comp.dashLengthBlend);
	}
};
