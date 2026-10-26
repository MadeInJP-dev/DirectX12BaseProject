#pragma once

#include "../IOption.h"

namespace Engine::Option::ProjectOptions
{
	// 項目を変えてから効くまで
	enum class EApplyTiming
	{
		Immediate,		// すぐ
		NextLoad,		// 次に読み込むものから(読み込み済みのものはそのまま)
		NextLaunch,		// 次の起動から
	};

	// 項目をまとめて決めるための組み合わせ。正本は各項目で、プリセットは値を入れるだけ
	// (並びは廃止した buildMode の値 Debug=0 / Development=1 / Shipping=2 と同じにしてある)
	enum class EBuildPreset
	{
		Debug,			// 開発 + D3D のデバッグレイヤー
		Development,	// 開発(エディター・JSON・デバッグキー)
		Submission,		// 提出用 : 開発用の機能をすべて切る
		Count
	};

	//==========================================================================================
	// 起動設定(エンジン設定の "BuildConfig")
	//
	// 以前のビルドモード(Debug / Development / Shipping)は、何が変わるかがコードの中にしか無かった。
	// いまは機能ごとの真偽値で持ち、何をするか・いつ効くか・各プリセットでの値は
	// GetBuildConfigItems() の一覧に1か所で書く(オプション画面・保存・プリセット・ビルドパネルはそこから引く)。
	//
	// ・最適化や _DEBUG のようにコンパイルで決まるものは VS の構成(Debug / Release)で決め、ここには置かない
	// ・使う側は MainEngine::GetBuildConfig()(今効いている値)を見る。
	//   OptionManager の値はオプション画面で直したもので、次の起動から効く項目はまだ効いていない
	//==========================================================================================
	struct BuildConfig : IOption
	{
		//-------------------------------------------------------------------------------------------
		// 機能 : 中身の説明は GetBuildConfigItems() の一覧
		//-------------------------------------------------------------------------------------------

		bool isD3DDebugLayer = false;
		bool isLoadJsonData = true;
		bool isRecordUsedAssets = true;
		bool isDebugKeys = true;
		bool isEditorEnabled = true;
		bool isWatchAssetFolder = true;

		//-------------------------------------------------------------------------------------------
		// パス・システムリソース
		//-------------------------------------------------------------------------------------------

		std::string assetRootPath = "Asset/";
		UINT maxThreadCount = 4;			// 使用できるスレッド最大数

		//-------------------------------------------------------------------------------------------
		// 項目の操作
		//-------------------------------------------------------------------------------------------

		// プリセットの値を各項目へ入れる
		void ApplyPreset(EBuildPreset a_preset);

		// 今の項目の組み合わせと同じプリセット。どれとも違えば無し
		std::optional<EBuildPreset> FindMatchingPreset() const;

		// a_edited の項目のうち、起動し直さなくても効くものだけを写す(MainEngine が毎フレーム呼ぶ)
		void ApplyLiveValues(const BuildConfig& a_edited);

		//-------------------------------------------------------------------------------------------
		// IOption
		//-------------------------------------------------------------------------------------------

		const std::string& GetName() const override
		{
			static const std::string NAME = "BuildConfig";
			return NAME;
		}

		EOptionCategory GetCategory() override
		{
			return EOptionCategory::Project;
		}

		void DrawEdit(const ECS::EngineServices& a_services) override;

		// 古い設定(buildMode だけのもの)は、そのモードのプリセットへ置き換えて読む
		void Archive(Persistence::Archive& a_archive) override;
	};

	//==========================================================================================
	// 起動設定の1項目の説明
	//==========================================================================================
	struct BuildConfigItem
	{
		const char* key = "";						// 保存キー
		const char* label = "";						// 表示名
		const char* description = "";				// 何をするか(OFF にすると何が無くなるか)
		EApplyTiming timing = EApplyTiming::NextLaunch;
		bool BuildConfig::* pValue = nullptr;		// 値の場所
		std::array<bool, static_cast<size_t>(EBuildPreset::Count)> presetValueArray = {};	// 各プリセットでの値
	};

	/// 起動設定の項目の一覧(何をするか・いつ効くか・プリセットの値の正本)
	///
	/// @return 表示する順に並んだ項目
	std::span<const BuildConfigItem> GetBuildConfigItems();

	/// プリセットの表示名
	const char* GetBuildPresetName(EBuildPreset a_preset);

	/// 反映の時期の表示名
	const char* GetApplyTimingName(EApplyTiming a_timing);
}
