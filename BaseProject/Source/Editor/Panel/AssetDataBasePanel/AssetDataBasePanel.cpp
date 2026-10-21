#include "AssetDataBasePanel.h"
#include "Engine/Resource/Data/AnimatorAsset/IO/AnimatorAssetIO.h"
#include "Engine/Resource/Data/Particles/IO/ParticlesIO.h"
#include "Engine/Resource/Data/AudioBehavior/IO/AudioBehaviorIO.h"
#include "Engine/Resource/Data/EffectAsset/IO/EffectAssetIO.h"
#include "Engine/Resource/Data/UIMaterial/IO/UIMaterialIO.h"
#include "Engine/Resource/Data/UIPreset/IO/UIPresetIO.h"
#include "Engine/Resource/Data/EffectPrefab/EffectPrefab.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

#include "../../Helper/EditorHelper.h"

#include "Engine/Scene/SceneManager/SceneManager.h"

// パイプラインの作成には、パス一覧(GraphicsEngine が持つ)が要る
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/RenderingPipeline/IO/RenderingPipelineAssetIO.h"

namespace Editor
{
	AssetDataBasePanel::AssetDataBasePanel()
	{
		m_assetCreateFuncs["AnimatorAsset"] = [](const ECS::EngineServices& a_services, const std::string& path, const std::string& name) {
			Resource::AnimatorAssetIO::Create(*a_services.pResourceManager, path, name);
			};

		m_assetCreateFuncs["ParticlesAsset"] = [](const ECS::EngineServices& a_services, const std::string& path, const std::string& name) {
			Resource::ParticlesAssetIO::Create(*a_services.pAssetDatabase, path, name);
			};

		m_assetCreateFuncs["Prefab"] = [](const ECS::EngineServices& a_services, const std::string& path, const std::string& name) {
			Resource::Prefab::Create(*a_services.pAssetDatabase, a_services.pSceneManager->RefWorld(), path, name);
			};

		m_assetCreateFuncs["EffectPrefab"] = [](const ECS::EngineServices& a_services, const std::string& path, const std::string& name) {
			Resource::EffectPrefab::Create(*a_services.pAssetDatabase, a_services.pSceneManager->RefWorld(), path, name);
			};

		m_assetCreateFuncs["AudioBehavior"] = [](const ECS::EngineServices& a_services, const std::string& path, const std::string& name) {
			Resource::AudioBehaviorIO::Create(*a_services.pAssetDatabase, path, name);
			};

		m_assetCreateFuncs["EffectAsset"] = [](const ECS::EngineServices& a_services, const std::string& path, const std::string& name) {
			Resource::EffectAssetIO::Create(*a_services.pAssetDatabase, path, name);
			};

		m_assetCreateFuncs["UIMaterial"] = [](const ECS::EngineServices& a_services, const std::string& path, const std::string& name) {
			Resource::UIMaterialIO::Create(*a_services.pAssetDatabase, path, name);
			};

		// UIプリセット : 既定の飾り(白い板ポリ)で作る。飾りから作るときは飾りのインスペクターの Save As Preset
		m_assetCreateFuncs["UIPreset"] = [](const ECS::EngineServices& a_services, const std::string& path, const std::string& name) {
			Resource::UIPresetIO::Create(*a_services.pAssetDatabase, path, name);
			};

		// レンダリングパイプライン : パスの一覧はグラフィックスエンジンが持っているので引いて渡す
		m_assetCreateFuncs["RenderingPipelineAsset"] = [](const ECS::EngineServices& a_services, const std::string& path, const std::string& name) {
			auto* _pGE = MainEngine::Instance().RefGraphicsEngine();
			Graphics::Pipeline::RenderingPipelineAssetIO::Create(
				*a_services.pAssetDatabase, path, name, _pGE ? _pGE->RefPassMetaRegistry() : nullptr);
			};

		// 空のシーン。作るだけで開かない(開くのはシーンビューのメニュー)
		m_assetCreateFuncs["Scene"] = [](const ECS::EngineServices& a_services, const std::string& path, const std::string& name) {
			a_services.pSceneManager->CreateEmptyScene(*a_services.pAssetDatabase, path, name);
			};
	}
	void AssetDataBasePanel::OnDrawImGui(EditorContext& a_editContext)
	{
		a_editContext.eInspectorType = EInspectorType::Asset;

		CreateAssetButton(a_editContext);

		AssetDataBaseExplorer(a_editContext);
	}
	void AssetDataBasePanel::CreateAssetButton(EditorContext& a_editContext)
	{
		if (Engine::EditorField::CreateButton("Create New Asset..."))
		{
			ImGui::OpenPopup("CreateResourcePopup");
		}

		// ポップアップの中身
		if (ImGui::BeginPopup("CreateResourcePopup"))
		{
			Engine::EditorField::Header("Select Asset Type");

			// データベースから現在登録されている全てのアセットタイプを取得
			auto _typeMap = a_editContext.pServices->pAssetDatabase->GetAssetTypeExtensionsMap();

			// ループで動的にUIを生成する！
			for (const auto& [_typeName, _extensions] : _typeMap)
			{
				// ファクトリに登録されていないタイプ（Createできないもの）はスキップする安全策
				if (!m_assetCreateFuncs.contains(_typeName)) continue;

				// ツリーノードの生成
				if (ImGui::TreeNodeEx(_typeName.c_str()))
				{
					Engine::EditorField::Field("Name", m_nameCach, sizeof(m_nameCach));
					Engine::EditorField::Field("FilePath", m_pathCach, sizeof(m_pathCach));

					if (Engine::EditorField::CreateButton("Create"))
					{
						// 辞書から該当する関数を引っ張ってきて実行！
						m_assetCreateFuncs[_typeName](
							*a_editContext.pServices, std::string(m_pathCach), std::string(m_nameCach));

						// キャッシュのクリア
						std::memset(m_nameCach, 0, sizeof(m_nameCach));
						std::memset(m_pathCach, 0, sizeof(m_pathCach));

						// 作成したらポップアップを閉じる
						ImGui::CloseCurrentPopup();
					}
					ImGui::TreePop();
				}
			}
			ImGui::EndPopup();
		}
	}
	void AssetDataBasePanel::AssetDataBaseExplorer(EditorContext& a_editContext)
	{
		// ファイル名でアセットを探す。出しっぱなしの欄なので入力は消さない
		const std::string& _search = EditorHelper::DrawSearchBox("##AssetSearch", "Search asset...", false);
		Engine::EditorField::Line();

		// 表示対象のアセットか : タブの種別と検索文字列の両方を満たすもの
		auto _isShowAsset = [&_search](const Resource::AssetProperty* a_pAsset, const std::string& a_filter)
			{
				if (!a_pAsset) return false;
				if (a_filter != "All" && a_pAsset->type != a_filter) return false;
				return EditorHelper::IsMatchSearch(_search, a_pAsset->fileName);
			};

		// 再帰的にツリーを描画する関数
		auto _drawNodeFunc = [&]
			(
				const std::string& a_name,
				const Resource::AssetNode& a_node,
				const std::string& a_tabName,
				auto& a_self
			)
			{
				// 子ノードの中に表示対象のアセットが一つでもあるか確認
				// (空のフォルダだけが並ぶのを防ぐ。絞り込み中は一致するものを持つ階層だけが残る)
				auto _hasMatchingAsset = [&](
					const Resource::AssetNode& a_node,
					const std::string& a_filter,
					auto& a_checkSelf
					) -> bool
					{
						for (auto* a : a_node.assets) { if (_isShowAsset(a, a_filter)) return true; }
						for (auto& c : a_node.children) { if (a_checkSelf(c.second, a_filter, a_checkSelf)) return true; }
						return false;
					};

				// アセットが入っていなければリターン
				if (!_hasMatchingAsset(a_node, a_tabName, _hasMatchingAsset)) return;

				bool _nodeOpen = true;

				// 名前が空の場合はルートノードなので中身を描画する
				auto _defaultFlag = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_Framed;
				if (!a_name.empty())
				{
					_nodeOpen = ImGui::TreeNodeEx(a_name.c_str(), _defaultFlag | ImGuiTreeNodeFlags_DefaultOpen);
				}

				// 上階層が開いていたら子ノードを描画
				if (_nodeOpen)
				{
					// フォルダ（子ノードを描画）
					for (auto& _child : a_node.children)
					{
						a_self(_child.first, _child.second, a_tabName, a_self);
					}

					// ファイル（アセットを描画）
					for (auto* _asset : a_node.assets)
					{
						// Allタブ以外の場合、Typeが一致しないファイルを除外
						// 検索中は名前が一致しないものも除外
						if (!_isShowAsset(_asset, a_tabName)) { continue; }

						bool _sel = (a_editContext.selectedAssetGUID == _asset->guid);
						if (ImGui::Selectable(_asset->fileName.c_str(), _sel))
						{
							a_editContext.SelectAsset(_asset);
						}
					}
					if (!a_name.empty())
					{
						ImGui::TreePop();
					}
				}
			};

		// タブバーを作成
		if (ImGui::BeginTabBar("AssetTabs"))
		{
			// アセットの構造階層を取得
			const auto& _rootNode = a_editContext.pServices->pAssetDatabase->GetAssetRootNode();
			const auto& _types = a_editContext.pServices->pAssetDatabase->GetAssetTypeExtensionsMap();
			for (auto& [_type, _typeExt] : _types)
			{
				if (ImGui::BeginTabItem(_type.c_str()))
				{
					// キャッシュされたツリーを描画
					_drawNodeFunc("", _rootNode, _type, _drawNodeFunc);
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}
	}
}