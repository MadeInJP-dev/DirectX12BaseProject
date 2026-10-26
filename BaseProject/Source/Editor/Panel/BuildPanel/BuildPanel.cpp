#include "BuildPanel.h"

#include "../../Helper/EditorHelper.h"
#include "Converter/AssetConverterRegistry.h"
#include "BuildPackage/BuildPlan/BuildPlan.h"
#include "BuildPackage/AssetCollector/AssetCollector.h"
#include "BuildPackage/SourceDirectoryWrite/SourceDirectoryWrite.h"
#include "BuildPackage/ExecuteDirectoryWrite/ExecuteDirectoryWrite.h"

#include "Engine/Scene/BaseScene/BaseScene.h"
#include "Engine/Scene/SceneManager/SceneManager.h"

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

#include <shellapi.h>

namespace Editor
{
	namespace
	{
		// コンバートして出来たファイルから参照が増えるので、集め直す回数の上限
		constexpr int MAX_CONVERT_PASS = 4;

		// 結果に並べる注意の上限(多いと表示が重い。全部はログへ出す)
		constexpr size_t MAX_REPORT_LINES = 500;

		// 実行フォルダの EngineData に書くビルドモードの選択肢(-1 はそのまま)
		constexpr std::array<const char*, 4> BUILD_MODE_LABELS = { "そのまま", "Debug", "Development", "Shipping" };

		// 大きさを読みやすく
		std::string FormatBytes(uintmax_t a_bytes)
		{
			constexpr std::array<const char*, 4> UNITS = { "B", "KB", "MB", "GB" };
			double _value = static_cast<double>(a_bytes);
			size_t _unit = 0;
			while (_value >= 1024.0 && _unit + 1 < UNITS.size())
			{
				_value /= 1024.0;
				++_unit;
			}

			char _buf[32] = {};
			std::snprintf(_buf, sizeof(_buf), _unit == 0 ? "%.0f %s" : "%.2f %s", _value, UNITS[_unit]);
			return _buf;
		}

		// a_path が a_dir の中(または同じ)か
		bool IsInsideDirectory(const std::filesystem::path& a_path, const std::filesystem::path& a_dir)
		{
			std::error_code _ec;
			const auto _path = std::filesystem::weakly_canonical(a_path, _ec);
			const auto _dir = std::filesystem::weakly_canonical(a_dir, _ec);
			if (_ec) return false;

			const auto _relative = _path.lexically_relative(_dir);
			return !_relative.empty() && *_relative.begin() != "..";
		}

		// 入力欄に入れるものの種類
		enum class EValueKind
		{
			Text,				// ただの文字列(拡張子・種別名など)
			DirectoryPath,		// フォルダのパス : エクスプローラーで選べる
			DirectoryName,		// フォルダ名だけ : エクスプローラーで選んだフォルダの名前を入れる
		};

		//==================================================================================
		// エクスプローラーでフォルダを選ぶ。選んだら a_value を書き換えて true
		//
		// 設定のパスは作業ディレクトリ(BaseProject/)からの相対で持つので、
		// プロジェクトの中のフォルダなら相対("Source" / "../Library" など)へ直す。外なら絶対パスのまま
		//==================================================================================
		bool SelectDirectory(std::string& a_value, EValueKind a_kind)
		{
			// 今の値の場所から開く(フォルダ名だけのものは作業ディレクトリから)
			const std::string _initialDir = (a_kind == EValueKind::DirectoryPath) ? a_value : "";

			std::string _selected = "";
			if (!EditorHelper::GetDirectoryNameOpenExplorer(_selected, _initialDir) || _selected.empty()) return false;

			const std::filesystem::path _path = Core::File::FromUTF8(_selected);
			if (a_kind == EValueKind::DirectoryName)
			{
				a_value = Core::File::ToUTF8(_path.filename());
				return true;
			}

			std::error_code _ec;
			const std::filesystem::path _cwd = std::filesystem::current_path(_ec);
			if (_ec || !IsInsideDirectory(_path, _cwd.parent_path()))
			{
				a_value = _selected;
				return true;
			}

			const std::u8string _relative =
				std::filesystem::weakly_canonical(_path, _ec).lexically_relative(_cwd).generic_u8string();
			a_value = _relative.empty() ? _selected : std::string(_relative.begin(), _relative.end());
			return true;
		}

		// 1行の文字列の入力欄。フォルダならエクスプローラーの選択も付ける。変えたら true
		bool DrawStringField(const char* a_label, std::string& a_value, EValueKind a_kind = EValueKind::Text)
		{
			bool _isChanged = false;

			ImGui::TextUnformatted(a_label);
			ImGui::PushID(a_label);
			if (a_kind != EValueKind::Text)
			{
				if (ImGui::Button("選択")) _isChanged |= SelectDirectory(a_value, a_kind);
				ImGui::SameLine();
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			_isChanged |= ImGui::InputText("##value", &a_value);
			ImGui::PopID();
			return _isChanged;
		}

		// 文字列の一覧の編集(行ごとの入力欄・削除・追加)。フォルダならエクスプローラーの選択も付ける。変えたら true
		bool DrawStringList(
			const char* a_label,
			std::vector<std::string>& a_valueVec,
			EValueKind a_kind = EValueKind::Text,
			const char* a_tooltip = nullptr
		)
		{
			bool _isChanged = false;
			const bool _isDirectory = (a_kind != EValueKind::Text);

			ImGui::PushID(a_label);
			ImGui::TextUnformatted(a_label);
			if (a_tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", a_tooltip);

			int _removeIndex = -1;
			for (size_t _i = 0; _i < a_valueVec.size(); ++_i)
			{
				ImGui::PushID(static_cast<int>(_i));
				if (ImGui::Button("x")) _removeIndex = static_cast<int>(_i);
				ImGui::SameLine();
				if (_isDirectory)
				{
					if (ImGui::Button("選択")) _isChanged |= SelectDirectory(a_valueVec[_i], a_kind);
					ImGui::SameLine();
				}
				ImGui::SetNextItemWidth(-FLT_MIN);
				_isChanged |= ImGui::InputText("##item", &a_valueVec[_i]);
				ImGui::PopID();
			}

			if (_removeIndex >= 0)
			{
				a_valueVec.erase(a_valueVec.begin() + _removeIndex);
				_isChanged = true;
			}

			// フォルダはエクスプローラーで選んで足す。まだ無いフォルダなどを手で書くときは空の行を足す
			if (_isDirectory)
			{
				if (ImGui::Button("+ フォルダを選んで追加"))
				{
					std::string _value = "";
					if (SelectDirectory(_value, a_kind))
					{
						a_valueVec.push_back(std::move(_value));
						_isChanged = true;
					}
				}
				ImGui::SameLine();
			}
			if (ImGui::Button(_isDirectory ? "+ 手で追加" : "+ 追加"))
			{
				a_valueVec.emplace_back();
				_isChanged = true;
			}
			ImGui::PopID();
			return _isChanged;
		}
	}

	BuildPanel::BuildPanel()
	{
		// 設定ファイルが無ければ既定値(見本の形)のまま
		Build::LoadBuildSettings(m_settings);
	}

	BuildPanel::~BuildPanel()
	{
		// 書き出し中に閉じられたら止めて待つ(スレッドはこのパネルのメンバーを触っている)
		m_isCancelRequested = true;
		if (m_buildThread.joinable()) m_buildThread.join();
	}

	void BuildPanel::OnDrawImGui(EditorContext& a_editContext)
	{
		UpdateBuildThread();

		// まとめてのコンバートを進める(ポップアップを閉じても続く)
		m_assetConvertView.Update(a_editContext);

		// ポップアップ準備
		m_assetConvertPopup.SetContent(
			[this, &a_editContext]()
			{
				m_assetConvertView.Draw(a_editContext);
			}
		);
		m_overwritePopup.SetContent(
			[this, &a_editContext]()
			{
				ImGui::Text("書き出し先にすでにファイルがあります。");
				ImGui::TextUnformatted(Core::File::ToUTF8(m_reportPackageDirectory).c_str());
				ImGui::Spacing();

				if (ImGui::Button("消して作り直す"))
				{
					BuildPackage(a_editContext, true);
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("上書きする"))
				{
					BuildPackage(a_editContext, false);
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("やめる"))
				{
					ImGui::CloseCurrentPopup();
				}
			}
		);

		const bool _isBuilding = m_isBuilding.load();
		const bool _isConverting = m_assetConvertView.IsConverting();

		// 書き出し中は設定を触らせない。まとめてのコンバート中も書き出さない(写す元が書き換わる)
		ImGui::BeginDisabled(_isBuilding || _isConverting);
		{
			// ビルド書き出しディレクトリ選択
			SelectBuildDirectory();

			if (DrawSettings(a_editContext)) m_isSettingsDirty = true;

			ImGui::Separator();

			if (ImGui::Button("確認"))
			{
				PreviewPackage(a_editContext);
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("入れるアセットと注意を出すだけ(書き出さない・コンバートしない)");

			ImGui::SameLine();

			if (ImGui::Button("書き出す"))
			{
				RequestBuildPackage(a_editContext);
			}

			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();

		}
		ImGui::EndDisabled();

		// コンバートの画面は、まとめてのコンバート中でも開き直して進み具合を見られるようにする
		ImGui::SameLine();
		ImGui::BeginDisabled(_isBuilding);
		if (ImGui::Button("AssetConvert"))
		{
			m_assetConvertView.RequestRefresh();
			m_assetConvertPopup.Open();
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("アセットを1件ずつ、または未変換をまとめてコンバートする");
		if (_isConverting)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("コンバート中…");
		}

		DrawBuildState();
		DrawReport(a_editContext);

		// ポップアップ描画
		m_assetConvertPopup.Draw();
		m_overwritePopup.Draw();

		// 入力中は保存しない(1文字ごとに書かない)
		if (m_isSettingsDirty && !ImGui::IsAnyItemActive())
		{
			Build::SaveBuildSettings(m_settings);
			m_isSettingsDirty = false;
		}
	}

	void BuildPanel::SelectBuildDirectory()
	{
		ImGui::TextUnformatted("出力先");
		ImGui::SameLine();
		if (ImGui::Button("選択"))
		{
			std::string _dir = "";
			if (EditorHelper::GetDirectoryNameOpenExplorer(_dir, m_settings.outputDirectory) && !_dir.empty())
			{
				m_settings.outputDirectory = _dir;
				m_isSettingsDirty = true;
			}
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::InputText("##OutputDirectory", &m_settings.outputDirectory)) m_isSettingsDirty = true;

		ImGui::TextUnformatted("パッケージ名");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::InputText("##PackageName", &m_settings.packageName)) m_isSettingsDirty = true;

		// 実際にできる場所
		if (!m_settings.outputDirectory.empty() && !m_settings.packageName.empty())
		{
			const auto _packageDir = Core::File::FromUTF8(m_settings.outputDirectory) / Core::File::FromUTF8(m_settings.packageName);
			ImGui::TextDisabled("→ %s", Core::File::ToUTF8(_packageDir).c_str());
		}
		else
		{
			ImGui::TextDisabled("→ 出力先とパッケージ名を決めてください");
		}
	}

	bool BuildPanel::DrawSettings(EditorContext& a_editContext)
	{
		bool _isChanged = false;
		auto& _settings = m_settings;

		// 作るもの : 見出しはフォルダ名(名前を変えても ID は変わらないよう ### で固定)
		_isChanged |= ImGui::Checkbox((_settings.sourceDirectoryName + "###WriteSource").c_str(), &_settings.isWriteSource);
		ImGui::SameLine();
		ImGui::BeginDisabled(!_settings.isWriteSource);
		_isChanged |= ImGui::Checkbox((_settings.libraryDirectoryName + "###WriteLibrary").c_str(), &_settings.isWriteLibrary);
		ImGui::EndDisabled();
		ImGui::SameLine();
		_isChanged |= ImGui::Checkbox((_settings.executeDirectoryName + "###WriteExecute").c_str(), &_settings.isWriteExecute);

		if (ImGui::CollapsingHeader("フォルダ名"))
		{
			ImGui::Indent();
			_isChanged |= DrawStringField("ソースフォルダ", _settings.sourceDirectoryName);
			_isChanged |= DrawStringField("実行フォルダ", _settings.executeDirectoryName);
			_isChanged |= DrawStringField("ライブラリフォルダ(vcxproj は ..\\Library を見るので、変えるとそのままではビルドできない)", _settings.libraryDirectoryName);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("ソースフォルダの中身"))
		{
			ImGui::Indent();
			_isChanged |= DrawStringField("ソース", _settings.sourceDirectory, EValueKind::DirectoryPath);
			_isChanged |= DrawStringList("直下に置くファイル", _settings.projectFileVec);
			_isChanged |= DrawStringField("シェーダー", _settings.shaderDirectory, EValueKind::DirectoryPath);
			_isChanged |= DrawStringList("シェーダーのソースの拡張子", _settings.shaderSourceExtensionVec);
			_isChanged |= DrawStringField("ライブラリ", _settings.libraryDirectory, EValueKind::DirectoryPath);
			_isChanged |= DrawStringList("ライブラリで入れないフォルダ名", _settings.libraryExcludeDirectoryVec, EValueKind::DirectoryName);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("実行フォルダの中身"))
		{
			ImGui::Indent();
			_isChanged |= DrawStringField("実行ファイル", _settings.exePath);
			_isChanged |= DrawStringField("置くときの名前(空なら元の名前)", _settings.exeName);
			_isChanged |= ImGui::Checkbox("exe と同じフォルダの dll も置く", &_settings.isCopyDll);
			_isChanged |= DrawStringList("一緒に置くファイル(説明書など)", _settings.extraFileVec);

			// -1(そのまま)が先頭なので 1 ずらして選ぶ
			int _modeIndex = std::clamp(_settings.executeBuildMode + 1, 0, static_cast<int>(BUILD_MODE_LABELS.size()) - 1);
			ImGui::TextUnformatted("EngineData のビルドモード");
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::Combo("##BuildMode", &_modeIndex, BUILD_MODE_LABELS.data(), static_cast<int>(BUILD_MODE_LABELS.size())))
			{
				_settings.executeBuildMode = _modeIndex - 1;
				_isChanged = true;
			}
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("実行フォルダのアセット"))
		{
			ImGui::Indent();
			_isChanged |= ImGui::Checkbox("書き出す前に未変換・古いものをコンバートする", &_settings.isConvertBeforeBuild);

			ImGui::TextDisabled("入れるアセットの起点(ここから中の GUID とパスを辿る)");
			{
				size_t _recordedCount = 0;
				if (a_editContext.pServices && a_editContext.pServices->pSceneManager)
				{
					_recordedCount = a_editContext.pServices->pSceneManager->GetUsedAssetGUIDs().size();
				}
				const std::string _label = "このセッションで読み込んだもの(" + std::to_string(_recordedCount) + " 件)";
				_isChanged |= ImGui::Checkbox(_label.c_str(), &_settings.isIncludeRecordedAssets);
			}
			_isChanged |= ImGui::Checkbox("ソースコードに直書きされた GUID とパス", &_settings.isScanSourceReferences);
			_isChanged |= ImGui::Checkbox("シーンを全部入れる", &_settings.isIncludeAllScenes);
			_isChanged |= DrawStringList("起動時に読む設定(Data)", _settings.dataDirectoryVec, EValueKind::DirectoryPath, "中の .ob* を置き、中に書いてある GUID も辿る");
			_isChanged |= DrawStringList("中を全部入れるフォルダ", _settings.alwaysIncludeDirectoryVec, EValueKind::DirectoryPath, "パスで直に読むもの(シェーダーなど)");
			_isChanged |= DrawStringList("入れないフォルダ", _settings.excludeDirectoryVec, EValueKind::DirectoryPath);
			_isChanged |= DrawStringList("入れない種別", _settings.excludeTypeVec, EValueKind::Text, "AssetProperty の Type(ObjectPrefab など)");
			_isChanged |= DrawStringList("JSON のまま置く拡張子", _settings.jsonKeepExtensionVec, EValueKind::Text, "JSON 固定で読むもの(バイナリが無い)");
			ImGui::Unindent();
		}

		if (ImGui::Button("設定を既定に戻す"))
		{
			// 出力先とパッケージ名は人ごとに違うので残す
			Build::BuildSettings _default = {};
			_default.outputDirectory = _settings.outputDirectory;
			_default.packageName = _settings.packageName;
			_settings = std::move(_default);
			_isChanged = true;
		}

		return _isChanged;
	}

	//======================================================================================
	// 1回ぶんの情報
	//--------------------------------------------------------------------------------------
	// 書き出し先は「消して作り直す」の対象になるので、ここで危ない指定をはじく
	//======================================================================================
	bool BuildPanel::CreateContext(EditorContext& a_editContext, Build::BuildContext& a_outContext, std::string& a_outError) const
	{
		const auto& _settings = m_settings;

		if (!a_editContext.pServices || !a_editContext.pServices->pResourceManager)
		{
			a_outError = "リソースマネージャーがありません";
			return false;
		}
		if (_settings.outputDirectory.empty())
		{
			a_outError = "出力先を選んでください";
			return false;
		}

		// パッケージ名はフォルダ1段だけ(区切りや .. を入れると、消す範囲が出力先の外へ出る)
		const std::filesystem::path _packageName = Core::File::FromUTF8(_settings.packageName);
		if (_settings.packageName.empty() || _packageName.has_parent_path() || _packageName.has_root_path()
			|| _packageName == "." || _packageName == "..")
		{
			a_outError = "パッケージ名はフォルダ名1つで書いてください(区切りや .. は使えません)";
			return false;
		}

		const std::filesystem::path _outputDir = Core::File::FromUTF8(_settings.outputDirectory);
		if (!_outputDir.is_absolute())
		{
			a_outError = "出力先は絶対パスで書いてください";
			return false;
		}

		const std::filesystem::path _packageDir = _outputDir / _packageName;

		// プロジェクトの中へ書くと、次の書き出しで自分自身を写し込む
		const std::filesystem::path _projectRoot = std::filesystem::current_path().parent_path();
		if (IsInsideDirectory(_packageDir, _projectRoot) || IsInsideDirectory(_projectRoot, _packageDir))
		{
			a_outError = "出力先はプロジェクト(" + Core::File::ToUTF8(_projectRoot) + ")の外にしてください";
			return false;
		}

		a_outContext = {};
		a_outContext.pSettings = &m_settings;
		a_outContext.pAssetDatabase = &a_editContext.pServices->pResourceManager->GetAssetDatabase();

		a_outContext.srcSourceDirectory = Core::File::FromUTF8(_settings.sourceDirectory);
		a_outContext.srcShaderDirectory = Core::File::FromUTF8(_settings.shaderDirectory);
		a_outContext.srcLibraryDirectory = Core::File::FromUTF8(_settings.libraryDirectory);
		a_outContext.srcExePath = Core::File::FromUTF8(_settings.exePath);

		a_outContext.packageDirectory = _packageDir;
		a_outContext.sourceDirectory = _packageDir / Core::File::FromUTF8(_settings.sourceDirectoryName);
		a_outContext.executeDirectory = _packageDir / Core::File::FromUTF8(_settings.executeDirectoryName);
		a_outContext.libraryDirectory = _packageDir / Core::File::FromUTF8(_settings.libraryDirectoryName);
		return true;
	}

	//======================================================================================
	// 置くものを決める
	//--------------------------------------------------------------------------------------
	// コンバートすると新しいファイル(モデルの .ojmdl など)から参照が増えるので、
	// コンバートしたものがあれば集め直す。変わらなくなったら終わり
	//======================================================================================
	void BuildPanel::PreparePlan(EditorContext& a_editContext, bool a_isConvert, Build::BuildContext& a_context, Build::BuildPlan& a_outPlan)
	{
		std::vector<Core::GUID> _recordedVec = {};
		if (a_editContext.pServices->pSceneManager)
		{
			_recordedVec = a_editContext.pServices->pSceneManager->GetUsedAssetGUIDs();
		}

		if (m_settings.isWriteExecute)
		{
			Build::AssetCollector _collector = {};
			std::unordered_set<Core::GUID> _triedSet = {};
			size_t _convertedCount = 0;
			size_t _failedCount = 0;

			for (int _pass = 0; _pass < MAX_CONVERT_PASS; ++_pass)
			{
				a_context.assetGUIDVec = _collector.Collect(a_context, _recordedVec);
				if (!a_isConvert) break;

				size_t _passConverted = 0;
				for (const auto& _guid : a_context.assetGUIDVec)
				{
					const auto* _pProp = a_context.pAssetDatabase->GetAssetProperty(_guid);
					if (!_pProp) continue;

					const auto* _pConverter = Build::AssetConverterRegistry::Find(_pProp->type);
					if (!_pConverter) continue;
					if (_pConverter->IsConverted(*_pProp) && !_pConverter->IsOutdated(*_pProp)) continue;

					// 実体の無いもの(古いメタファイルだけ残っているもの)は変換しようがない
					const bool _hasFile = std::any_of(_pProp->extensionsVec.begin(), _pProp->extensionsVec.end(),
						[_pProp](const std::string& a_ext)
						{
							std::error_code _ec;
							return std::filesystem::is_regular_file(_pProp->filePath + a_ext, _ec);
						});
					if (!_hasFile) continue;

					// 失敗したものを何度も試さない
					if (!_triedSet.insert(_guid).second) continue;

					if (Build::AssetConvertView::ConvertAsset(a_editContext, _guid)) ++_passConverted;
					else ++_failedCount;
				}

				_convertedCount += _passConverted;
				if (_passConverted == 0) break;
			}

			if (a_isConvert)
			{
				ENGINE_LOG("[Build] 書き出し前のコンバート : 成功 %zu / 失敗 %zu", _convertedCount, _failedCount);
			}

			Build::ExecuteDirectoryWrite _executeWrite = {};
			_executeWrite.Build(a_context, a_outPlan);
		}

		if (m_settings.isWriteSource)
		{
			Build::SourceDirectoryWrite _sourceWrite = {};
			_sourceWrite.Build(a_context, a_outPlan);
		}

		if (!m_settings.isWriteSource && !m_settings.isWriteExecute)
		{
			a_outPlan.AddWarning("作るもの(ソース / 実行)がどちらも選ばれていません");
		}
	}

	void BuildPanel::PreviewPackage(EditorContext& a_editContext)
	{
		Build::BuildContext _context = {};
		std::string _error = {};
		if (!CreateContext(a_editContext, _context, _error))
		{
			m_reportTitle = "確認できません : " + _error;
			m_upReportPlan.reset();
			m_reportAssetVec.clear();
			return;
		}

		auto _upPlan = std::make_unique<Build::BuildPlan>();
		PreparePlan(a_editContext, false, _context, *_upPlan);

		m_reportTitle = "確認 : " + std::to_string(_upPlan->GetEntryVec().size()) + " 件 / "
			+ FormatBytes(_upPlan->GetTotalBytes()) + " を書き出す予定";
		m_reportAssetVec = _context.assetGUIDVec;
		m_reportPackageDirectory = _context.packageDirectory;
		m_buildErrorVec.clear();
		m_upReportPlan = std::move(_upPlan);
	}

	void BuildPanel::RequestBuildPackage(EditorContext& a_editContext)
	{
		Build::BuildContext _context = {};
		std::string _error = {};
		if (!CreateContext(a_editContext, _context, _error))
		{
			m_reportTitle = "書き出せません : " + _error;
			m_upReportPlan.reset();
			m_reportAssetVec.clear();
			ENGINE_WARNING("[Build] %s", m_reportTitle.c_str());
			return;
		}

		// 前の書き出しが残っていたら、消すか上書きするかを聞く
		std::error_code _ec;
		if (std::filesystem::is_directory(_context.packageDirectory, _ec)
			&& !std::filesystem::is_empty(_context.packageDirectory, _ec))
		{
			m_reportPackageDirectory = _context.packageDirectory;
			m_overwritePopup.Open();
			return;
		}

		BuildPackage(a_editContext, false);
	}

	bool BuildPanel::BuildPackage(EditorContext& a_editContext, bool a_isClean)
	{
		if (m_isBuilding.load()) return false;

		Build::BuildContext _context = {};
		std::string _error = {};
		if (!CreateContext(a_editContext, _context, _error))
		{
			m_reportTitle = "書き出せません : " + _error;
			return false;
		}

		// 設定は書き出したものと揃えて残す
		Build::SaveBuildSettings(m_settings);
		m_isSettingsDirty = false;

		auto _upPlan = std::make_unique<Build::BuildPlan>();
		PreparePlan(a_editContext, m_settings.isConvertBeforeBuild, _context, *_upPlan);

		for (const auto& _warning : _upPlan->GetWarningVec())
		{
			ENGINE_WARNING("[Build] %s", _warning.c_str());
		}

		m_reportAssetVec = _context.assetGUIDVec;
		m_reportPackageDirectory = _context.packageDirectory;
		m_reportTitle = "書き出し中 : " + Core::File::ToUTF8(_context.packageDirectory);
		m_buildErrorVec.clear();
		m_upReportPlan = std::move(_upPlan);

		// ここから先はワーカースレッド。予定(m_upReportPlan)は終わるまで作り直さない
		m_doneCount = 0;
		m_doneBytes = 0;
		m_isCancelRequested = false;
		m_isBuildSucceeded = false;
		m_isBuilding = true;

		m_buildThread = std::thread(
			[this, a_isClean, _packageDir = _context.packageDirectory]()
			{
				bool _isSuccess = true;

				if (a_isClean)
				{
					std::error_code _ec;
					std::filesystem::remove_all(Core::File::ToLongPath(_packageDir), _ec);
					if (_ec)
					{
						m_buildErrorVec.push_back("書き出し先を消せませんでした : "
							+ Core::String::ToUTF8(Core::String::ToWideString(_ec.message())));
						_isSuccess = false;
					}
				}

				if (_isSuccess)
				{
					_isSuccess = m_upReportPlan->Execute(m_doneCount, m_doneBytes, m_isCancelRequested, m_buildErrorVec);
				}

				m_isBuildSucceeded = _isSuccess && !m_isCancelRequested.load();
				m_isBuilding = false;
			});

		return true;
	}

	void BuildPanel::UpdateBuildThread()
	{
		// 終わったスレッドの片付け(結果はスレッドが止まってから読む)
		if (m_isBuilding.load() || !m_buildThread.joinable()) return;
		m_buildThread.join();

		const size_t _warningCount = m_upReportPlan ? m_upReportPlan->GetWarningVec().size() : 0;

		if (m_isCancelRequested.load())
		{
			m_reportTitle = "中止しました(途中までのファイルが残っています)";
		}
		else if (m_isBuildSucceeded)
		{
			m_reportTitle = "書き出しました : " + std::to_string(m_doneCount.load()) + " 件 / "
				+ FormatBytes(m_doneBytes.load()) + "(注意 " + std::to_string(_warningCount) + " 件)";
		}
		else
		{
			m_reportTitle = "書き出しに失敗したファイルがあります : " + std::to_string(m_buildErrorVec.size()) + " 件";
		}

		ENGINE_LOG("[Build] %s", m_reportTitle.c_str());
		for (const auto& _error : m_buildErrorVec)
		{
			ENGINE_WARNING("[Build] 書き出し失敗 : %s", _error.c_str());
		}
	}

	void BuildPanel::DrawBuildState()
	{
		if (!m_isBuilding.load() || !m_upReportPlan) return;

		const size_t _total = m_upReportPlan->GetEntryVec().size();
		const uintmax_t _totalBytes = m_upReportPlan->GetTotalBytes();
		const uintmax_t _doneBytes = m_doneBytes.load();

		// 進み具合は大きさで見る(小さいメタファイルが大量にあるので件数だと偏る)
		const float _fraction = _totalBytes > 0
			? static_cast<float>(static_cast<double>(_doneBytes) / static_cast<double>(_totalBytes))
			: 0.0f;

		const std::string _overlay = std::to_string(m_doneCount.load()) + " / " + std::to_string(_total)
			+ "  (" + FormatBytes(_doneBytes) + " / " + FormatBytes(_totalBytes) + ")";

		ImGui::ProgressBar(_fraction, ImVec2(-FLT_MIN, 0.0f), _overlay.c_str());
		if (ImGui::Button("中止"))
		{
			m_isCancelRequested = true;
		}
	}

	void BuildPanel::DrawReport(EditorContext& a_editContext)
	{
		if (m_reportTitle.empty()) return;

		ImGui::Separator();
		ImGui::TextWrapped("%s", m_reportTitle.c_str());

		if (!m_isBuilding.load() && !m_reportPackageDirectory.empty())
		{
			std::error_code _ec;
			if (std::filesystem::is_directory(m_reportPackageDirectory, _ec) && ImGui::Button("フォルダを開く"))
			{
				ShellExecuteW(nullptr, L"open", m_reportPackageDirectory.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			}
		}

		// 書き出し中はスレッドが m_buildErrorVec に書くので見せない
		if (!m_isBuilding.load() && !m_buildErrorVec.empty())
		{
			const std::string _label = "失敗 (" + std::to_string(m_buildErrorVec.size()) + ")###BuildErrors";
			if (ImGui::CollapsingHeader(_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (size_t _i = 0; _i < m_buildErrorVec.size() && _i < MAX_REPORT_LINES; ++_i)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_buildErrorVec[_i].c_str());
				}
			}
		}

		if (!m_upReportPlan) return;

		const auto& _warningVec = m_upReportPlan->GetWarningVec();
		if (!_warningVec.empty())
		{
			const std::string _label = "注意 (" + std::to_string(_warningVec.size()) + ")###BuildWarnings";
			if (ImGui::CollapsingHeader(_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (size_t _i = 0; _i < _warningVec.size() && _i < MAX_REPORT_LINES; ++_i)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", _warningVec[_i].c_str());
				}
				if (_warningVec.size() > MAX_REPORT_LINES) ImGui::TextDisabled("…ほかはログを見てください");
			}
		}

		const std::string _assetLabel = "入れるアセット (" + std::to_string(m_reportAssetVec.size()) + ")###BuildAssets";
		if (ImGui::CollapsingHeader(_assetLabel.c_str()))
		{
			const auto* _pResourceManager = a_editContext.pServices ? a_editContext.pServices->pResourceManager : nullptr;
			if (!_pResourceManager) return;
			const auto& _assetDatabase = _pResourceManager->GetAssetDatabase();

			// 数千件になるので見えている行だけ描く
			ImGuiListClipper _clipper;
			_clipper.Begin(static_cast<int>(m_reportAssetVec.size()));
			while (_clipper.Step())
			{
				for (int _i = _clipper.DisplayStart; _i < _clipper.DisplayEnd; ++_i)
				{
					const auto* _pProp = _assetDatabase.GetAssetProperty(m_reportAssetVec[static_cast<size_t>(_i)]);
					if (!_pProp) continue;

					ImGui::Text("[%s] %s", _pProp->type.c_str(),
						Core::File::ToUTF8(std::filesystem::path(_pProp->filePath)).c_str());
				}
			}
		}
	}
}
