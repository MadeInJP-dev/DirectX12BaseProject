#include "BuildConfig.h"

#include "Engine/MainEngine.h"
#include "Engine/ECS/System/SystemContext.h"	// EngineServices
#include "Engine/EditorField/EditorField.h"

namespace Engine::Config
{
	namespace
	{
		constexpr size_t PRESET_COUNT = static_cast<size_t>(EBuildPreset::Count);

		//==================================================================================
		// 項目の一覧(正本)
		//
		// 項目を足すときはここに1行足し、使う側は MainEngine::GetBuildConfig() で引く。
		// プリセットの値は { デバッグ, 開発, 提出 } の順
		//==================================================================================
		const std::array<BuildConfigItem, 6> ITEMS =
		{{
			{
				"isD3DDebugLayer", "D3D のデバッグレイヤー",
				"D3D12 のデバッグレイヤーを有効にし、終了時に解放し忘れた GPU オブジェクトを報告する。重くなる",
				EApplyTiming::NextLaunch, &BuildConfig::isD3DDebugLayer, { true, false, false }
			},
			{
				"isLoadJsonData", "開発用の JSON(.oj*)を優先して読む",
				"OFF ならバイナリ(.ob*)だけを読む(提出用)。ON でも .oj* が無ければ .ob* を読む。"
				"モデル・メッシュ・アニメーションはどちらでもバイナリ",
				EApplyTiming::NextLoad, &BuildConfig::isLoadJsonData, { true, true, false }
			},
			{
				"isRecordUsedAssets", "読み込んだアセットを記録する",
				"ビルドパネルの「このセッションで読み込んだもの」に使う",
				EApplyTiming::NextLaunch, &BuildConfig::isRecordUsedAssets, { true, true, false }
			},
			{
				"isDebugKeys", "デバッグキー(F1〜F7)",
				"発表・確認用のキー(無敵・敵を倒すなど)を使えるようにする",
				EApplyTiming::NextLaunch, &BuildConfig::isDebugKeys, { true, true, false }
			},
			{
				"isEditorEnabled", "エディターへ切り替えられる",
				"OFF にするとゲームからエディターへ戻れない。戻すときは Asset/Data/Engine/EngineData.ojoptn を直す",
				EApplyTiming::NextLaunch, &BuildConfig::isEditorEnabled, { true, true, false }
			},
			{
				"isWatchAssetFolder", "アセットフォルダを監視する",
				"起動中に置いた・消したファイルを拾い、.assetmeta を作る。起動時のメタファイル作りは OFF でも行う",
				EApplyTiming::NextLaunch, &BuildConfig::isWatchAssetFolder, { true, true, false }
			},
		}};
	}

	std::span<const BuildConfigItem> GetBuildConfigItems()
	{
		return ITEMS;
	}

	const char* GetBuildPresetName(EBuildPreset a_preset)
	{
		switch (a_preset)
		{
		case EBuildPreset::Debug:		return "デバッグ";
		case EBuildPreset::Development:	return "開発";
		case EBuildPreset::Submission:	return "提出";
		default:						return "";
		}
	}

	const char* GetApplyTimingName(EApplyTiming a_timing)
	{
		switch (a_timing)
		{
		case EApplyTiming::Immediate:	return "すぐ";
		case EApplyTiming::NextLoad:	return "次に読み込むものから";
		case EApplyTiming::NextLaunch:	return "次の起動から";
		default:						return "";
		}
	}

	void BuildConfig::ApplyPreset(EBuildPreset a_preset)
	{
		const size_t _index = static_cast<size_t>(a_preset);
		if (_index >= PRESET_COUNT) return;

		for (const auto& _item : ITEMS)
		{
			this->*_item.pValue = _item.presetValueArray[_index];
		}
	}

	std::optional<EBuildPreset> BuildConfig::FindMatchingPreset() const
	{
		for (size_t _index = 0; _index < PRESET_COUNT; ++_index)
		{
			const bool _isMatch = std::all_of(ITEMS.begin(), ITEMS.end(),
				[this, _index](const BuildConfigItem& a_item) { return this->*a_item.pValue == a_item.presetValueArray[_index]; });
			if (_isMatch) return static_cast<EBuildPreset>(_index);
		}
		return std::nullopt;
	}

	void BuildConfig::ApplyLiveValues(const BuildConfig& a_edited)
	{
		for (const auto& _item : ITEMS)
		{
			if (_item.timing == EApplyTiming::NextLaunch) continue;

			// ワーカースレッドの読み込みからも引かれるので、変わったときだけ書く
			if (this->*_item.pValue != a_edited.*_item.pValue) this->*_item.pValue = a_edited.*_item.pValue;
		}
	}

	//======================================================================================
	// オプション画面
	//--------------------------------------------------------------------------------------
	// 項目ごとに「いつ効くか」を横に出し、次の起動から効く項目が今と違えばそう出す
	//======================================================================================
	void BuildConfig::DrawEdit(const ECS::EngineServices& a_services)
	{
		const BuildConfig* _pActive = a_services.pMainEngine ? &a_services.pMainEngine->GetBuildConfig() : nullptr;

		// プリセット
		Engine::EditorField::Text("プリセット");
		for (size_t _index = 0; _index < PRESET_COUNT; ++_index)
		{
			const auto _preset = static_cast<EBuildPreset>(_index);
			Engine::EditorField::SameLine();
			if (Engine::EditorField::Button(GetBuildPresetName(_preset))) ApplyPreset(_preset);
		}
		const auto _matchPreset = FindMatchingPreset();
		Engine::EditorField::HelpText("今の組み合わせ : %s",
			_matchPreset ? GetBuildPresetName(*_matchPreset) : "どのプリセットとも違う");

		// 項目
		for (const auto& _item : ITEMS)
		{
			bool& _value = this->*_item.pValue;
			Engine::EditorField::Field(_item.label, _value);
			Engine::EditorField::Tooltip("%s", _item.description);
			Engine::EditorField::SameLine();
			Engine::EditorField::HelpText("(%s)", GetApplyTimingName(_item.timing));

			if (_pActive && _item.timing == EApplyTiming::NextLaunch && _pActive->*_item.pValue != _value)
			{
				Engine::EditorField::WarningText("  次の起動から %s になります(今は %s)", _value ? "ON" : "OFF", _value ? "OFF" : "ON");
			}
		}

		// 食い違う組み合わせ
		if (isEditorEnabled && !isLoadJsonData)
		{
			Engine::EditorField::WarningText("エディターで直して保存しても、読み込むのはバイナリ(.ob*)なので JSON だけの直しは反映されません");
		}
		if (isEditorEnabled && !isWatchAssetFolder)
		{
			Engine::EditorField::WarningText("起動中に置いたファイルにはメタファイルが作られず、エディターから選べません");
		}
		if (!isEditorEnabled)
		{
			Engine::EditorField::WarningText("次の起動からエディターへ戻れなくなります");
		}

		Engine::EditorField::Line();
		Engine::EditorField::Field("AssetRootPath", assetRootPath);
		int _count = static_cast<int>(maxThreadCount);
		Engine::EditorField::Field("maxThreadCount", _count);
		maxThreadCount = static_cast<UINT>(_count);
	}

	void BuildConfig::Archive(Persistence::Archive& a_archive)
	{
		// 古い設定 : buildMode(0=Debug / 1=Development / 2=Shipping)しか無いので、
		// そのモードのプリセットで埋めてから、書いてある項目で上書きする。
		// エンジン設定は JSON 固定で読む(ConfigRegistry)ので、無いキーは読み飛ばされる
		if (a_archive.IsLoading())
		{
			int _legacyMode = -1;
			a_archive.Field("buildMode", _legacyMode);
			if (_legacyMode >= 0 && static_cast<size_t>(_legacyMode) < PRESET_COUNT)
			{
				ApplyPreset(static_cast<EBuildPreset>(_legacyMode));
			}
		}

		for (const auto& _item : ITEMS)
		{
			a_archive.Field(_item.key, this->*_item.pValue);
		}

		a_archive.Field("assetRootPath", assetRootPath);
		a_archive.Field("maxThreadCount", maxThreadCount);
	}
}
