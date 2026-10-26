#include "AssetConvertView.h"

#include "../Converter/AssetConverterRegistry.h"

#include "Editor/Internal/EditorContext.h"

#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

#include <chrono>

namespace Editor::Build
{
	namespace
	{
		// まとめてのコンバートで1フレームに使ってよい時間(これを超えたら次のフレームへ)。
		// 1件はかならず進めるので、重いモデルだとこれより長く止まる
		constexpr auto CONVERT_TIME_BUDGET = std::chrono::milliseconds(30);

		// 一覧の表の高さ
		constexpr float TABLE_HEIGHT = 400.0f;
		constexpr float TABLE_WIDTH = 900.0f;
	}

	//======================================================================================
	// 1件コンバートする
	//--------------------------------------------------------------------------------------
	// アセット種別ごとの処理は Converter/ 以下。ここは引いて呼ぶだけ
	//======================================================================================
	bool AssetConvertView::ConvertAsset(EditorContext& a_editContext, const Core::GUID& a_guid)
	{
		if (!a_editContext.pServices || !a_editContext.pServices->pResourceManager) return false;

		auto* _pResourceManager = a_editContext.pServices->pResourceManager;
		const auto* _pProp = _pResourceManager->GetAssetDatabase().GetAssetProperty(a_guid);
		if (!_pProp) return false;

		const auto* _pConverter = AssetConverterRegistry::Find(_pProp->type);
		if (!_pConverter)
		{
			ENGINE_WARNING("[Build] この種別のコンバーターがありません : %s (%s)",
				_pProp->type.c_str(), _pProp->fileName.c_str());
			return false;
		}

		const std::string _filePath = _pResourceManager->GetAssetDatabase().GetFilePathFromGUID(a_guid);

		AssetConvertContext _context =
		{
			*_pResourceManager,
			*_pProp,
			_filePath,
			Core::File::GetDirFromPath(_filePath),
			Core::File::GetFileNameWithoutExtension(_filePath),
			a_editContext.pServices->pSceneManager ? a_editContext.pServices->pSceneManager->RefWorld() : nullptr
		};

		std::string _error = {};
		if (!_pConverter->Convert(_context, _error))
		{
			ENGINE_WARNING("[Build] コンバート失敗 : %s (%s) %s",
				_pProp->type.c_str(), _filePath.c_str(), _error.c_str());
			return false;
		}

		ENGINE_LOG("[Build] コンバート完了 : %s (%s)", _pProp->type.c_str(), _filePath.c_str());
		return true;
	}

	void AssetConvertView::Update(EditorContext& a_editContext)
	{
		if (!m_isConverting) return;

		const auto _startTime = std::chrono::steady_clock::now();
		do
		{
			if (m_isCancelRequested || m_queueIndex >= m_queueVec.size())
			{
				m_isConverting = false;
				m_isTargetDirty = true;

				const size_t _failedCount = m_failedLabelVec.size();
				m_resultText = std::string(m_isCancelRequested ? "中止しました" : "終わりました")
					+ " : 成功 " + std::to_string(m_successCount) + " / 失敗 " + std::to_string(_failedCount)
					+ " / 残り " + std::to_string(m_queueVec.size() - m_queueIndex);
				ENGINE_LOG("[Build] 一括コンバート %s", m_resultText.c_str());
				return;
			}

			Target& _target = m_targetVec[m_queueVec[m_queueIndex]];
			if (ConvertAsset(a_editContext, _target.guid))
			{
				_target.isConverted = true;
				++m_successCount;
				if (m_unconvertedCount > 0) --m_unconvertedCount;
			}
			else
			{
				m_failedLabelVec.push_back(_target.label);
			}
			++m_queueIndex;
		}
		while (std::chrono::steady_clock::now() - _startTime < CONVERT_TIME_BUDGET);
	}

	void AssetConvertView::Draw(EditorContext& a_editContext)
	{
		if (!a_editContext.pServices || !a_editContext.pServices->pResourceManager) return;

		if (m_isTargetDirty && !m_isConverting) RefreshTargets(a_editContext);

		// まとめてのコンバート中は対象を変えさせない(一覧の添え字で回しているため)
		ImGui::BeginDisabled(m_isConverting);
		{
			ImGui::TextUnformatted("対象");
			ImGui::SameLine();
			int _scope = static_cast<int>(m_scope);
			bool _isScopeChanged = ImGui::RadioButton("このセッションで読み込んだもの", &_scope, static_cast<int>(EScope::Recorded));
			ImGui::SameLine();
			_isScopeChanged |= ImGui::RadioButton("すべてのアセット", &_scope, static_cast<int>(EScope::All));
			if (_isScopeChanged)
			{
				m_scope = static_cast<EScope>(_scope);
				m_isTargetDirty = true;
			}

			ImGui::Checkbox("未変換だけ出す", &m_isShowUnconvertedOnly);
			ImGui::SameLine();
			if (ImGui::Button("一覧を更新")) m_isTargetDirty = true;

			ImGui::Text("未変換 %zu 件 / 全 %zu 件", m_unconvertedCount, m_targetVec.size());

			ImGui::BeginDisabled(m_unconvertedCount == 0);
			const std::string _label = "未変換をすべてコンバート (" + std::to_string(m_unconvertedCount) + " 件)";
			if (ImGui::Button(_label.c_str())) StartConvertAll();
			ImGui::EndDisabled();
		}
		ImGui::EndDisabled();

		DrawProgress();

		ImGui::Separator();

		DrawTargetTable(a_editContext);
	}

	void AssetConvertView::RefreshTargets(EditorContext& a_editContext)
	{
		m_isTargetDirty = false;
		m_targetVec.clear();
		m_unconvertedCount = 0;

		const auto& _assetDatabase = a_editContext.pServices->pResourceManager->GetAssetDatabase();

		// 対象の GUID
		std::vector<Core::GUID> _guidVec = {};
		if (m_scope == EScope::Recorded)
		{
			if (a_editContext.pServices->pSceneManager)
			{
				_guidVec = a_editContext.pServices->pSceneManager->GetUsedAssetGUIDs();
			}
		}
		else
		{
			for (const auto& [_type, _typeExt] : _assetDatabase.GetAssetTypeExtensionsMap())
			{
				for (const auto& _prop : _assetDatabase.GetTypeMetaVec(_type))
				{
					_guidVec.push_back(_prop.guid);
				}
			}
		}

		for (const auto& _guid : _guidVec)
		{
			const auto* _pProp = _assetDatabase.GetAssetProperty(_guid);
			if (!_pProp) continue;

			Target _target = {};
			_target.guid = _guid;
			_target.label = "[" + _pProp->type + "] " + Core::File::ToUTF8(std::filesystem::path(_pProp->filePath));

			const auto* _pConverter = AssetConverterRegistry::Find(_pProp->type);
			_target.hasConverter = (_pConverter != nullptr);
			_target.isConverted = _pConverter && _pConverter->IsConverted(*_pProp);
			_target.hasFile = std::any_of(_pProp->extensionsVec.begin(), _pProp->extensionsVec.end(),
				[_pProp](const std::string& a_ext)
				{
					std::error_code _ec;
					return std::filesystem::is_regular_file(_pProp->filePath + a_ext, _ec);
				});

			if (IsUnconverted(_target)) ++m_unconvertedCount;
			m_targetVec.push_back(std::move(_target));
		}

		// 探しやすいようパス順に
		std::sort(m_targetVec.begin(), m_targetVec.end(),
			[](const Target& a_lhs, const Target& a_rhs) { return a_lhs.label < a_rhs.label; });
	}

	void AssetConvertView::StartConvertAll()
	{
		m_queueVec.clear();
		for (size_t _i = 0; _i < m_targetVec.size(); ++_i)
		{
			if (IsUnconverted(m_targetVec[_i])) m_queueVec.push_back(_i);
		}
		if (m_queueVec.empty()) return;

		m_queueIndex = 0;
		m_successCount = 0;
		m_failedLabelVec.clear();
		m_resultText.clear();
		m_isCancelRequested = false;
		m_isConverting = true;
	}

	void AssetConvertView::DrawProgress()
	{
		if (m_isConverting)
		{
			const size_t _total = m_queueVec.size();
			const float _fraction = _total > 0 ? static_cast<float>(m_queueIndex) / static_cast<float>(_total) : 0.0f;
			const std::string _overlay = std::to_string(m_queueIndex) + " / " + std::to_string(_total);
			ImGui::ProgressBar(_fraction, ImVec2(TABLE_WIDTH, 0.0f), _overlay.c_str());

			// 次に変換するもの(このフレームの終わりまで画面は止まるので、次のフレームで回るもの)
			if (m_queueIndex < _total)
			{
				ImGui::Text("次 : %s", m_targetVec[m_queueVec[m_queueIndex]].label.c_str());
			}
			ImGui::Text("成功 %zu / 失敗 %zu", m_successCount, m_failedLabelVec.size());
			ImGui::SameLine();
			if (ImGui::Button("中止")) m_isCancelRequested = true;
		}
		else if (!m_resultText.empty())
		{
			ImGui::TextUnformatted(m_resultText.c_str());
		}

		if (!m_failedLabelVec.empty())
		{
			const std::string _label = "失敗したもの (" + std::to_string(m_failedLabelVec.size()) + ")###ConvertFailed";
			if (ImGui::TreeNode(_label.c_str()))
			{
				for (const auto& _failed : m_failedLabelVec)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", _failed.c_str());
				}
				ImGui::TextDisabled("理由はログを見てください");
				ImGui::TreePop();
			}
		}
	}

	void AssetConvertView::DrawTargetTable(EditorContext& a_editContext)
	{
		// 出す行 : 「未変換だけ」のときは絞る
		std::vector<size_t> _rowVec = {};
		_rowVec.reserve(m_targetVec.size());
		for (size_t _i = 0; _i < m_targetVec.size(); ++_i)
		{
			if (m_isShowUnconvertedOnly && !IsUnconverted(m_targetVec[_i])) continue;
			_rowVec.push_back(_i);
		}

		// 個別でコンバートする : マーク・ボタン・名前を列でそろえる
		if (!ImGui::BeginTable("ConvertTargets", 3,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
			ImVec2(TABLE_WIDTH, TABLE_HEIGHT)))
		{
			return;
		}
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 24.0f);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		// 「すべて」は数千件になるので見えている行だけ描く
		ImGuiListClipper _clipper;
		_clipper.Begin(static_cast<int>(_rowVec.size()));
		while (_clipper.Step())
		{
			for (int _row = _clipper.DisplayStart; _row < _clipper.DisplayEnd; ++_row)
			{
				Target& _target = m_targetVec[_rowVec[static_cast<size_t>(_row)]];
				ImGui::PushID(_row);
				ImGui::TableNextRow();

				// 1列目 : コンバート済みかどうか(緑：変換済み / 赤：未変換 / 灰：実体が無い)
				ImGui::TableSetColumnIndex(0);
				{
					const ImU32 _color = !_target.hasFile ? IM_COL32(120, 120, 120, 255)
						: _target.isConverted ? IM_COL32(50, 220, 100, 255)
						: IM_COL32(240, 70, 70, 255);

					const ImVec2 _cursorPos = ImGui::GetCursorScreenPos();
					const float _radius = 5.0f;
					ImGui::GetWindowDrawList()->AddCircleFilled(
						ImVec2(_cursorPos.x + _radius + 2.0f, _cursorPos.y + ImGui::GetFrameHeight() * 0.5f),
						_radius, _color);
					ImGui::Dummy(ImVec2(_radius * 2.0f + 4.0f, ImGui::GetFrameHeight()));
				}

				// 2列目 : コンバートボタン(まとめて回している間は押させない)
				ImGui::TableSetColumnIndex(1);
				ImGui::BeginDisabled(!_target.hasConverter || !_target.hasFile || m_isConverting);
				if (ImGui::Button("Convert"))
				{
					if (ConvertAsset(a_editContext, _target.guid))
					{
						if (!_target.isConverted && m_unconvertedCount > 0) --m_unconvertedCount;
						_target.isConverted = true;
					}
				}
				ImGui::EndDisabled();

				// 3列目 : 種別と名前
				ImGui::TableSetColumnIndex(2);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(_target.label.c_str());

				ImGui::PopID();
			}
		}

		ImGui::EndTable();
	}
}
