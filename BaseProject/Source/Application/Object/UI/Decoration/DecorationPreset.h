#pragma once

#include "Application/AppCommon.h"

//==========================================================================================
// 飾りとプリセット(UIPreset)のつながり
//
// 飾りはプリセットを GUID で指し、付いていく部分(見た目・大きさ・反応・アニメ・中身)を選ぶ。
// プリセットの中身が変わるたび(UIPreset::revision が進むたび)、付いていく部分を写し直す。
// 付いていかない部分と、置き場所(位置・回転・Z順)・名前・表示はいつも飾りの値のまま。
//
// 飾りは写した値を自分でも持っている(描画・判定・保存は今までどおり飾りの値だけを見る)。
// プリセットが見つからないときも、最後に写した値で出る。
//
// 実装は DecorationPreset.cpp(写す・読み書き・インスペクター)
//==========================================================================================
namespace Engine::ECS { struct EngineServices; }

namespace App::Object::Decoration
{
	struct Decoration;

	//======================================================================================
	// プリセットのどこに付いていくか
	//======================================================================================
	enum class EPresetPart : uint32_t
	{
		NONE		= 0,
		LOOK		= 1 << 0,	// 種類・色・枠と UV(板ポリ・画像)・フォントと文字の組み方(文字)
		SIZE		= 1 << 1,	// 大きさ・倍率・ピボット
		REACTION	= 1 << 2,	// カーソルへの反応
		ANIMATION	= 1 << 3,	// トゥイーン・揺れ
		CONTENT		= 1 << 4,	// 中身 : 文字列・画像のテクスチャ

		ALL			= LOOK | SIZE | REACTION | ANIMATION | CONTENT,
	};
	ENUM_ATTR_BITFLAG(EPresetPart);

	// つないだときに付いていく部分 : 大きさと中身は飾りごとに違うことが多いので、既定では付いていかない
	inline constexpr EPresetPart DEFAULT_PRESET_FOLLOW = EPresetPart::LOOK | EPresetPart::REACTION | EPresetPart::ANIMATION;

	//======================================================================================
	// プリセットとのつながり : 飾り1つが1つ持つ
	//======================================================================================
	struct PresetLink
	{
		//---------------------------------------
		// 保存される
		//---------------------------------------
		Core::GUID presetGUID = {};						// 指しているプリセット。空ならつながっていない
		EPresetPart follow = DEFAULT_PRESET_FOLLOW;		// 付いていく部分

		//---------------------------------------
		// ランタイム(保存しない)
		//---------------------------------------
		Engine::ResourceRef<Engine::Resource::UIPreset> presetRef = {};
		uint32_t appliedRevision = 0;	// 最後に写したときのプリセットの番号
		bool isApplied = false;			// 写したか。付いていく部分を変えたら下ろして写し直す
		bool isRequested = false;		// 読み込みを頼んだか(見つからないプリセットを毎フレーム頼み直さないため)
		bool isEditPreset = false;		// エディター : 付いていく部分をこの飾りで直し、プリセットへ書き戻すか

		bool IsLinked() const { return presetGUID.IsValid(); }
		bool IsFollow(EPresetPart a_part) const { return IsLinked() && Core::HasFlag(follow, a_part); }

		// 付いていく部分を、この飾りのインスペクターで触れないようにするか
		bool IsLocked(EPresetPart a_part) const { return IsFollow(a_part) && !isEditPreset; }
	};

	//======================================================================================
	// 操作
	//======================================================================================

	/// <summary>
	/// a_preset の a_parts の部分を a_dst へ写す
	/// </summary>
	/// <remarks>置き場所(位置・回転・Z順)・名前・番号・表示・つながりは写さない</remarks>
	void ApplyPresetParts(Decoration& a_dst, const Decoration& a_preset, EPresetPart a_parts);

	// 飾り1つぶんを、プリセットに入れる JSON の文字列にする(つながりは書かない)
	std::string SerializePresetDecoration(const Decoration& a_decoration);

	// プリセットの JSON の文字列から飾りを作る。空なら既定の飾り。読めなければ false
	bool DeserializePresetDecoration(const std::string& a_json, Decoration& a_outDecoration);

	// 指すプリセットを変えたあとに呼ぶ : 読み込みを頼み、次の SyncPreset で写し直させる
	void RequestPreset(Decoration& a_decoration, Engine::Resource::ResourceManager* a_pResourceManager);

	/// <summary>
	/// プリセットが変わっていれば写し直す
	/// </summary>
	/// <returns>写したら true(呼び出し側でテクスチャ・フォントを引き直すこと)</returns>
	/// <remarks>プリセットがまだ届いていない間は何もしない(飾りの値のまま出る)</remarks>
	bool SyncPreset(Decoration& a_decoration, Engine::Resource::ResourceManager* a_pResourceManager);

	/// <summary>
	/// この飾りの付いていく部分を、プリセットへ書き戻す
	/// </summary>
	/// <remarks>プリセットの番号が進むので、同じプリセットを指す飾りが全部写し直す。ファイルへは保存しない</remarks>
	void PushToPreset(const Decoration& a_decoration, Engine::Resource::ResourceManager* a_pResourceManager);

	/// <summary>
	/// インスペクター : プリセットの指し替え・付いていく部分・書き戻し・新しく作る
	/// </summary>
	/// <returns>つながりが変わったら true</returns>
	bool DrawPresetInspector(Decoration& a_decoration, const Engine::ECS::EngineServices& a_services);
}
