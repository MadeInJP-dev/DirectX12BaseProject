#pragma once

#include "../IPanel.h"
#include "Editor/EditorCommon.h"

namespace Editor
{
	//==========================================================================================
	// リリース用、提出用のビルドを書き出すためのクラス
	// 
	// 使用アセットを一括で指定フォルダにコピー
	//==========================================================================================
	class BuildPanel : public IPanel
	{
	public:

		BuildPanel();
		~BuildPanel() override = default;

		const char* GetName() const override { return "BuildPanel"; };
		void OnDrawImGui(EditorContext& a_editContext) override;

	private:

		//-------------------------------------------------------------------------------------------
		// ビルド設定準備
		//-------------------------------------------------------------------------------------------

		// 親ディレクトリ選択
		void SelectBuildDirectory();

		// 使用するアセット全部を再コンバートする
		void UseAssetConvert(EditorContext& a_editContext);

		// 1件コンバートする : アセット種別ごとの処理は Converter/ 以下。成功したら true
		bool ConvertAsset(EditorContext& a_editContext, const Core::GUID& a_guid);

	private:

		// 書き出し先親フォルダ
		std::string m_buildDirectory = "";


	};
}