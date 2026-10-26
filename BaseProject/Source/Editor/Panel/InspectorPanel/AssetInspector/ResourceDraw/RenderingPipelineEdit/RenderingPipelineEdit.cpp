#include "RenderingPipelineEdit.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Editor/Helper/EditorHelper.h"
#include "Editor/Internal/EditorContext.h"

#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"
#include "Engine/Graphics/RenderingPipeline/Internal/Connection.h"
#include "Engine/Graphics/RenderingPipeline/RenderGraph/RenderGraph.h"
#include "Engine/Graphics/RenderingPipeline/PassMetaRegistry/PassMetaRegistry.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPipelineAsset/RenderingPipelineAsset.h"
#include "Engine/Graphics/RenderingPipeline/StandardPipeline/StandardPipeline.h"

namespace Editor::Inspector
{
	using namespace Engine::Graphics::Pipeline;

	//======================================================================================
	//
	// 描き始めの確認
	//
	//======================================================================================
	bool RenderingPipelineEditor::OnBeginDraw(EditorContext& a_editContext)
	{
		(void)a_editContext;

		m_pAsset = nullptr;

		auto& _manager = *a_editContext.pServices->pResourceManager;

		// 読み込まれていなければ、ここで読み込む口だけ出す
		if (!_manager.Has<RenderingPipelineAsset>(m_assetGUID))
		{
			Engine::EditorField::Header("No loaded file");
			if (ImGui::Button("Load"))
			{
				_manager.LoadImmediate<RenderingPipelineAsset>(m_assetGUID);
			}
			return false;
		}

		auto _handle = _manager.GetCache<RenderingPipelineAsset>(m_assetGUID);
		m_pAsset = _manager.Ref(_handle);

		if (!m_pAsset)
		{
			Engine::EditorField::WarningText("Not found asset");
			return false;
		}

		// 読み込み直後はレジストリが入っていないことがあるので、ここで必ず通しておく
		auto* _pGE = MainEngine::Instance().RefGraphicsEngine();
		m_pAsset->SetMetaRegistry(_pGE ? _pGE->RefPassMetaRegistry() : nullptr);

		// 出口は常駐。レジストリが後から入った場合もここで揃う
		m_pAsset->EnsureFinalPass();

		RenderGraph* _pGraph = m_pAsset->RefRenderGraph();
		if (!_pGraph) return false;

		//----------------------------------------------------------------------------------
		// まとまりはフレームの頭で組み直す
		//
		// パスの増減は描き終わってから(OnPostDraw)しか起きないので、
		// ここで組んでおけば、そのフレームの間ずっと生きたポインタで通せる。
		// 前のフレームのものを持ち越すと、消えたパスを指したまま詳細欄が触りに行く
		//----------------------------------------------------------------------------------
		m_compositeGroups = BuildCompositeGroups(*_pGraph);

		return true;
	}

	//======================================================================================
	//
	// ヘッダー(グラフの外)
	//
	//======================================================================================
	void RenderingPipelineEditor::OnDrawHeader(EditorContext& a_editContext)
	{
		DrawToolbar(a_editContext);
		Engine::EditorField::Line();

		DrawValidation();
		DrawSelectedPassDetail();
	}

	void RenderingPipelineEditor::DrawToolbar(EditorContext& a_editContext)
	{
		// 保存。
		// ImNodes 上で動かしたノード座標を書き戻してから書き出す
		if (a_editContext.pAssetProp && ImGui::Button("Save"))
		{
			SyncNodePositions();
			m_pAsset->Save(a_editContext.pAssetProp->filePath);
		}

		// 画面が出ているかどうか。
		// 組めていないと画面は真っ黒になるので、ここで分かるようにしておく
		if (auto* _pGE = MainEngine::Instance().RefGraphicsEngine())
		{
			Engine::EditorField::SameLine();
			if (_pGE->GetCameraPipelines()->IsPipelinePresentActive())	Engine::EditorField::HelpText("| 画面 : 出力中");
			else								Engine::EditorField::HelpText("| 画面 : 出ていません");
		}

		Engine::EditorField::Line();

		DrawAddPass();
		Engine::EditorField::SameLine();
		DrawAddComposite();
		Engine::EditorField::SameLine();

		// 構成が変わっていなければ押しても結果は同じなので、そのときは通さない
		if (ImGui::Button("Compile") && m_pAsset->IsDirty())
		{
			m_pAsset->Compile();
		}
		Engine::EditorField::SameLine();
		if (m_pAsset->IsDirty())	Engine::EditorField::WarningText("Modified");
		else						Engine::EditorField::HelpText("Compiled");

		const RenderGraph* _pGraph = m_pAsset->GetRenderGraph();
		Engine::EditorField::SameLine();
		Engine::EditorField::HelpText("| Pass : %d", static_cast<int>(_pGraph->GetPasses().size()));

		// 既存の描画と同じ流れを一式組む。
		// 今入っているものは全部捨てるので、押し間違いが痛い分だけ確認を挟む
		Engine::EditorField::SameLine();
		if (ImGui::Button("Standard")) m_standardConfirmPopup.Open();
		m_standardConfirmPopup.Draw();
	}

	void RenderingPipelineEditor::InitStandardConfirm()
	{
		m_standardConfirmPopup.SetMessage("既定の構成で組み直しますか?", "今のパスと配線をすべて捨てて組み直します");
		m_standardConfirmPopup.AddChoice("Build",
			[this]()
			{
				// 確認は描画の中(DrawToolbar)で出すので、m_pAsset はこのフレームのものが入っている
				PassMetaRegistry* _pRegistry = m_pAsset ? m_pAsset->RefMetaRegistry() : nullptr;
				if (!_pRegistry) return;

				BuildStandardPipeline(*m_pAsset, *_pRegistry);

				// パスが総入れ替えになるので、次の Draw で座標を配り直す
				ReserveApplyNodePositions();
			},
			EConfirmStyle::Danger);
	}

	void RenderingPipelineEditor::DrawAddPass()
	{
		PassMetaRegistry* _pRegistry = m_pAsset->RefMetaRegistry();
		if (!_pRegistry)
		{
			Engine::EditorField::HelpText("No PassMetaRegistry");
			return;
		}

		if (EditorField::CreateButton("AddPass"))
		{
			ImGui::OpenPopup("AddPassPopup");
		}
		if (!ImGui::BeginPopup("AddPassPopup")) return;

		Engine::EditorField::HelpText("Select Pass");
		Engine::EditorField::Line();

		const auto& _allMeta = _pRegistry->GetAllMeta();
		if (_allMeta.empty())
		{
			Engine::EditorField::HelpText("No registered pass");
			ImGui::EndPopup();
			return;
		}

		// 検索用
		const std::string& _search = EditorHelper::DrawSearchBox();

		// クラス名順に並べて表示
		std::vector<ID<Pass>> _ids;
		_ids.reserve(_allMeta.size());
		for (const auto& [_id, _meta] : _allMeta) _ids.push_back(_id);
		std::sort(_ids.begin(), _ids.end(),
			[&_allMeta](ID<Pass> a_lhs, ID<Pass> a_rhs)
			{ return _allMeta.at(a_lhs).name < _allMeta.at(a_rhs).name; }
		);

		// 選択欄
		for (ID<Pass> _id : _ids)
		{
			const auto& _meta = _allMeta.at(_id);

			// 出口は常駐なので、手で足せないようにする
			if (_meta.isFinalPass) continue;

			if (!EditorHelper::IsMatchSearch(_search, _meta.name)) continue;

			std::string _label = _meta.name + "##addobj" + std::to_string(_id.value);
			if (ImGui::Selectable(_label.c_str()))
			{
				AddPassFromEditor(_id);
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::EndPopup();
	}

	//======================================================================================
	// 合成ノードの追加
	//
	// 一式まとめて置く。
	// パスを1つずつ足して手で繋ぐより間違えにくい
	//======================================================================================
	void RenderingPipelineEditor::DrawAddComposite()
	{
		if (EditorField::CreateButton("AddComposite"))
		{
			ImGui::OpenPopup("AddCompositePopup");
		}
		if (!ImGui::BeginPopup("AddCompositePopup")) return;

		Engine::EditorField::HelpText("Select Composite");
		Engine::EditorField::Line();

		for (const std::string& _typeName : m_compositeNodeRegistry.GetTypeNames())
		{
			ICompositeNode* _pNode = m_compositeNodeRegistry.Find(_typeName);
			if (!_pNode) continue;

			if (!ImGui::Selectable(_pNode->GetDisplayName())) continue;

			// 足したぶんだけ ImNodes へ置く。
			// 全体を配り直すと、動かしてあった他のノードが巻き戻る
			if (Pass* _pHead = _pNode->Build(*m_pAsset))
			{
				m_pAsset->SetDirty();

				const Math::Vector2& _pos = _pHead->GetEditorPos();
				ImNodes::SetNodeEditorSpacePos(_pHead->GetNodeID(), ImVec2(_pos.x, _pos.y));
			}
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	//======================================================================================
	// 出てきたノードの座標だけを配る
	//
	// まとまりを解いた直後は、今まで描いていなかったパスがノードとして出てくる。
	// ImNodes はその場所を知らないので原点に重なって出る。
	//
	// 全体を配り直す(ReserveApplyNodePositions)と、動かしてあった他のノードまで
	// 保存された位置へ巻き戻るので、増えたぶんだけを置く
	//======================================================================================
	void RenderingPipelineEditor::HandlePendingApplyPos()
	{
		if (m_pendingApplyPosVec.empty()) return;

		RenderGraph* _pGraph = m_pAsset->RefRenderGraph();
		if (!_pGraph) { m_pendingApplyPosVec.clear(); return; }

		for (const Core::GUID& _passGUID : m_pendingApplyPosVec)
		{
			Pass* _pPass = _pGraph->FindPass(_passGUID);
			if (!_pPass) continue;

			const Math::Vector2& _pos = _pPass->GetEditorPos();
			ImNodes::SetNodeEditorSpacePos(_pPass->GetNodeID(), ImVec2(_pos.x, _pos.y));
		}
		m_pendingApplyPosVec.clear();
	}

	//======================================================================================
	// ノードから出た要求を通す
	//
	// パスの増減はここで初めて起きる。
	// ノードを回している最中にやると、パス配列の反復が壊れる
	//======================================================================================
	void RenderingPipelineEditor::HandlePendingRequest()
	{
		if (!m_pendingRequestGroup.IsValid()) return;

		const Core::GUID _groupGUID = m_pendingRequestGroup;
		const CompositeNodeRequest _request = m_pendingRequest;
		m_pendingRequestGroup = {};
		m_pendingRequest = {};

		const CompositeGroup* _pGroup = nullptr;
		for (const CompositeGroup& _group : m_compositeGroups.groups)
		{
			if (_group.guid != _groupGUID) continue;
			_pGroup = &_group;
			break;
		}
		if (!_pGroup) return;

		ICompositeNode* _pNode = m_compositeNodeRegistry.Find(_pGroup->typeName);
		if (!_pNode) return;

		if (_request.resizeCount >= 0 && _pNode->Resize(*m_pAsset, *_pGroup, _request.resizeCount))
		{
			// 段が変わったので、中の配線も組み直す
			m_pendingSyncGroup = _groupGUID;
		}
	}

	//======================================================================================
	// まとまりの中の配線を組み直す
	//
	// 段数を変えた直後と、見せているピンへ線が引かれた直後に通す。
	// 隠している段の Depth / Normal はここで配られる
	//======================================================================================
	void RenderingPipelineEditor::HandlePendingSyncGroup()
	{
		if (!m_pendingSyncGroup.IsValid()) return;

		const Core::GUID _groupGUID = m_pendingSyncGroup;
		m_pendingSyncGroup = {};

		RenderGraph* _pGraph = m_pAsset->RefRenderGraph();
		if (!_pGraph) return;

		// 段数が変わっているので組み直してから引く
		const CompositeGroupTable _table = BuildCompositeGroups(*_pGraph);
		for (const CompositeGroup& _group : _table.groups)
		{
			if (_group.guid != _groupGUID) continue;

			if (ICompositeNode* _pNode = m_compositeNodeRegistry.Find(_group.typeName))
			{
				_pNode->SyncInternalLinks(*_pGraph, _group);
			}

			// ここで座標を配り直さないこと。
			//
			// ReserveApplyNodePositions() は「全ノードを保存された位置へ戻す」もので、
			// 配線を整えただけなのに動かしていない他のノードまで巻き戻ってしまう。
			// 増えた段はノードとして出さない(代表しか描かない)ので、
			// そもそも ImNodes へ座標を配る必要が無い
			break;
		}
	}

	// 繋ぎ方の不備をその場で見せる。
	// ログにしか出ないと、パスが増えたときにどのノードが原因か追えなくなる
	void RenderingPipelineEditor::DrawValidation()
	{
		RenderGraph* _pGraph = m_pAsset->RefRenderGraph();

		std::vector<ValidationIssue> _issueVec = {};
		const bool _isValid = _pGraph->Validate(&_issueVec);

		if (_issueVec.empty())
		{
			Engine::EditorField::WarningText("Validation : OK");
			return;
		}

		// エラーが1つでもあるとコンパイルは通らない
		if (_isValid)	Engine::EditorField::WarningText("Validation : %d warning(s)", static_cast<int>(_issueVec.size()));
		else			Engine::EditorField::ErrorText("Validation : NG");

		if (ImGui::TreeNodeEx("Issues", _isValid ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (const ValidationIssue& _issue : _issueVec)
			{
				if (_issue.level == ValidationIssue::ELevel::Error)
				{
					Engine::EditorField::ErrorText("Error : %s", _issue.message.c_str());
				}
				else
				{
					Engine::EditorField::WarningText("Warn : %s", _issue.message.c_str());
				}

				// クリックでそのノードを選ぶ
				if (!ImGui::IsItemClicked()) continue;
				if (!_issue.passGUID.IsValid()) continue;

				Pass* _pPass = _pGraph->FindPass(_issue.passGUID);
				if (!_pPass) continue;

				// コンテキストは Draw が立てているので、ここでは選び直すだけ
				ImNodes::ClearNodeSelection();
				ImNodes::SelectNode(_pPass->GetNodeID());
			}
			ImGui::TreePop();
		}
		Engine::EditorField::Line();
	}

	// 選択中のパスの詳細(パス固有の設定)を出す。
	// ノードの中に全部詰めると線が見えなくなるので、細かい設定はこちら側で編集する
	void RenderingPipelineEditor::DrawSelectedPassDetail()
	{
		const int _nodeID = GetSingleSelectedNodeID();
		if (_nodeID == 0) return;

		Pass* _pPass = m_pAsset->RefRenderGraph()->FindPassByNodeID(_nodeID);
		if (!_pPass) return;

		//----------------------------------------------------------------------------------
		// まとまりを選んでいるとき
		//
		// ノードIDは代表のパスのものなので、ここへ来るのも代表。
		// 段ごとの設定は合成ノード側が並べる
		//----------------------------------------------------------------------------------
		if (const CompositeGroup* _pGroup = m_compositeGroups.Find(_pPass->GetGUID()))
		{
			ICompositeNode* _pNode = m_compositeNodeRegistry.Find(_pGroup->typeName);
			if (_pNode && ImGui::CollapsingHeader("Selected Group", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::PushID(_nodeID);

				CompositeNodeRequest _request = {};
				switch (_pNode->DrawDetail(*_pGroup, m_passEditorRegistry, _request))
				{
				case EPassEditResult::Structure:	m_pAsset->SetDirty();		break;
				case EPassEditResult::Param:	m_pAsset->SetParamDirty();	break;
				default: break;
				}

				if (!_request.IsEmpty())
				{
					m_pendingRequestGroup = _pGroup->guid;
					m_pendingRequest = _request;
				}
				ImGui::PopID();
			}
			if (_pNode)
			{
				Engine::EditorField::Line();
				return;
			}
		}

		if (ImGui::CollapsingHeader("Selected Pass", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::PushID(_nodeID);
			Engine::EditorField::Text("%s", _pPass->GetName().c_str());
			Engine::EditorField::Line();

			// パス固有の設定(フォーマットやスケールなど)もリソースの要件を変えるので、
			// 触られたら Dirty にする。
			// 値を確定したところ(ドラッグを離した等)で1回だけ立つ。
			// パラメータだけなら組み直さず、カメラ側へ値を写すだけで済ませる
			IPassEditor* _pEditor = m_passEditorRegistry.Find(*_pPass);
			if (!_pEditor)
			{
				// 登録漏れ : 触れないだけで動きはするので、気づけるように出しておく
				Engine::EditorField::HelpText("編集UIが登録されていません");
			}
			else
			{
				switch (_pEditor->DrawDetail(*_pPass))
				{
				case EPassEditResult::Structure:	m_pAsset->SetDirty();			break;
				case EPassEditResult::Param:		m_pAsset->SetParamDirty();		break;
				default: break;
				}
			}

			ImGui::PopID();
		}
		Engine::EditorField::Line();
	}

	//======================================================================================
	//
	// グラフの中
	//
	//======================================================================================
	void RenderingPipelineEditor::OnDrawNodes(EditorContext& a_editContext)
	{
		(void)a_editContext;

		RenderGraph* _pGraph = m_pAsset->RefRenderGraph();

		// まとまりは OnBeginDraw で組んである。ここは出したピンの印だけ
		m_visiblePinSet.clear();

		//----------------------------------------------------------------------------------
		// ノード
		//
		// まとまりに属するパスは代表のときだけ描く。
		// 2段目以降はノードを出さないので、5段のデノイズでも見た目は1つ
		//----------------------------------------------------------------------------------
		for (auto& _upPass : _pGraph->GetPasses())
		{
			if (!_upPass) continue;

			const CompositeGroup* _pGroup = m_compositeGroups.Find(_upPass->GetGUID());
			if (!_pGroup)
			{
				DrawNode(*_upPass);
				continue;
			}

			// 代表(段の先頭)以外は出さない
			if (_pGroup->GetHead() != _upPass.get()) continue;

			ICompositeNode* _pNode = m_compositeNodeRegistry.Find(_pGroup->typeName);
			if (!_pNode)
			{
				// 登録漏れ : まとめずに個別のノードとして出しておく
				DrawNode(*_upPass);
				continue;
			}

			DrawCompositeNode(*_pGroup, *_pNode);
		}

		//----------------------------------------------------------------------------------
		// 線
		//
		// 両端ともノードに出ているものだけを描く。
		// まとまりが隠したピンへの線は、グラフには居るが見えないままになる
		//----------------------------------------------------------------------------------
		const auto& _connectionMap = _pGraph->GetConnections();
		for (auto& _upPass : _pGraph->GetPasses())
		{
			if (!_upPass) continue;

			auto _it = _connectionMap.find(_upPass->GetGUID());
			if (_it == _connectionMap.end()) continue;

			for (const Connection& _connection : _it->second)
			{
				const Slot* _pSrcSlot = _upPass->FindOutputSlot(_connection.srcSlotID);
				if (!_pSrcSlot) continue;

				Pass* _pDst = _pGraph->FindPass(_connection.dstPassGUID);
				if (!_pDst) continue;

				const Slot* _pDstSlot = _pDst->FindInputSlot(_connection.dstSlotID);
				if (!_pDstSlot) continue;

				if (!IsVisiblePin(_pSrcSlot->pinID) || !IsVisiblePin(_pDstSlot->pinID)) continue;

				_connection.EditConnection(_pSrcSlot->pinID, _pDstSlot->pinID);
			}
		}
	}

	//======================================================================================
	// まとまり1つ分のノード
	//
	// 枠と座標は代表のパスのものを使う。
	// ピンは合成ノードが「外へ見せる」と言ったものだけ
	//======================================================================================
	void RenderingPipelineEditor::DrawCompositeNode(const CompositeGroup& a_group, ICompositeNode& a_node)
	{
		Pass* _pHead = a_group.GetHead();
		if (!_pHead) return;

		std::vector<Slot*> _inputVec = {};
		std::vector<Slot*> _outputVec = {};
		a_node.CollectVisibleSlots(a_group, _inputVec, _outputVec);

		ImNodes::BeginNode(_pHead->GetNodeID());

		EditorHelper::DrawNodeTitleBar(a_node.MakeTitle(a_group));

		for (Slot* _pIn : _inputVec)
		{
			if (!_pIn) continue;

			m_visiblePinSet.insert(_pIn->pinID);

			ImNodes::BeginInputAttribute(_pIn->pinID);
			if (_pIn->IsConnected())	Engine::EditorField::Text("%s : %s", _pIn->pinName.c_str(), _pIn->name.c_str());
			else						Engine::EditorField::HelpText("%s", _pIn->pinName.c_str());
			ImNodes::EndInputAttribute();
		}

		for (Slot* _pOut : _outputVec)
		{
			if (!_pOut) continue;

			m_visiblePinSet.insert(_pOut->pinID);

			ImNodes::BeginOutputAttribute(_pOut->pinID);
			Engine::EditorField::Text("%s : %s", _pOut->pinName.c_str(), _pOut->name.c_str());
			ImNodes::EndOutputAttribute();
		}

		// ノードの中の操作(段数など)。
		// グラフを触るのは描き終わってからにしたいので、要求だけ控える
		CompositeNodeRequest _request = {};
		switch (a_node.DrawNode(a_group, _request))
		{
		case EPassEditResult::Structure:	m_pAsset->SetDirty();		break;
		case EPassEditResult::Param:	m_pAsset->SetParamDirty();	break;
		default: break;
		}

		if (!_request.IsEmpty())
		{
			m_pendingRequestGroup = a_group.guid;
			m_pendingRequest = _request;
		}

		if (EditorField::DeleteSmallButton("Ungroup"))
		{
			// 札を外すだけ。パスも線もそのままで、個別のノードに戻る
			for (Pass* _pMember : a_group.members)
			{
				if (!_pMember) continue;

				_pMember->ClearEditorGroup();

				// 今まで出していなかったノードが出てくる。
				// ImNodes が場所を知らないので、こいつらのぶんだけ配る
				m_pendingApplyPosVec.push_back(_pMember->GetGUID());
			}
		}

		ImNodes::EndNode();
	}

	void RenderingPipelineEditor::DrawNode(Pass& a_pass)
	{
		ImNodes::BeginNode(a_pass.GetNodeID());

		EditorHelper::DrawNodeTitleBar(a_pass.GetName());

		// 入力ピン : つながっていればリソース名まで出す
		for (const Slot& _in : a_pass.GetInputSlots())
		{
			m_visiblePinSet.insert(_in.pinID);

			ImNodes::BeginInputAttribute(_in.pinID);
			if (_in.IsConnected())
			{
				Engine::EditorField::Text("%s : %s", _in.pinName.c_str(), _in.name.c_str());
			}
			else
			{
				Engine::EditorField::HelpText("%s", _in.pinName.c_str());
			}
			ImNodes::EndInputAttribute();
		}

		// 出力ピン : 作るリソース名は宣言時に決まっている
		for (const Slot& _out : a_pass.GetOutputSlots())
		{
			m_visiblePinSet.insert(_out.pinID);

			ImNodes::BeginOutputAttribute(_out.pinID);
			Engine::EditorField::Text("%s : %s", _out.pinName.c_str(), _out.name.c_str());
			ImNodes::EndOutputAttribute();
		}

		// パス固有のノード内UI。
		// パスは ImGui を知らないので、種類ごとの編集UIをレジストリから引く
		if (IPassEditor* _pEditor = m_passEditorRegistry.Find(a_pass))
		{
			_pEditor->DrawNode(a_pass);
		}

		// 出口は常駐なので消させない
		if (!m_pAsset->IsFinalPass(a_pass))
		{
			// 削除は反復中に消すとイテレータが壊れるので予約だけする
			if (EditorField::DeleteSmallButton("Delete Pass"))
			{
				m_pendingDeletePass = a_pass.GetGUID();
			}
		}

		ImNodes::EndNode();
	}

	//======================================================================================
	//
	// 操作(EndNodeEditor の後でないと拾えないもの)
	//
	//======================================================================================
	void RenderingPipelineEditor::OnPostDraw(EditorContext& a_editContext)
	{
		(void)a_editContext;

		HandleDeleteSelection();
		HandlePendingDeletePass();
		HandleCreateLink();
		HandlePendingRequest();
		HandlePendingSyncGroup();
		HandlePendingApplyPos();
	}

	void RenderingPipelineEditor::HandleCreateLink()
	{
		int _startAttr = 0;
		int _endAttr = 0;
		if (!ImNodes::IsLinkCreated(&_startAttr, &_endAttr)) return;

		RenderGraph* _pGraph = m_pAsset->RefRenderGraph();

		// どちらが出力側で引かれたか分からないので、両端をそれぞれ判定する
		Pass* _pSrc = nullptr;
		Slot* _pSrcSlot = nullptr;
		Pass* _pDst = nullptr;
		Slot* _pDstSlot = nullptr;

		const int _attrs[2] = { _startAttr, _endAttr };
		for (int _attr : _attrs)
		{
			Slot* _pSlot = nullptr;
			bool _isInput = false;
			Pass* _pPass = _pGraph->FindPassByPinID(_attr, &_pSlot, &_isInput);
			if (!_pPass || !_pSlot) continue;

			if (_isInput)
			{
				_pDst = _pPass;
				_pDstSlot = _pSlot;
			}
			else
			{
				_pSrc = _pPass;
				_pSrcSlot = _pSlot;
			}
		}

		// 入力どうし・出力どうしをつないだ場合はここで弾かれる
		if (!_pSrc || !_pSrcSlot || !_pDst || !_pDstSlot) return;

		// 自分自身へのつなぎや、入力スロットの張り替えは RenderGraph 側が面倒を見る
		if (!_pGraph->Link(
			_pSrc->GetGUID(), _pSrcSlot->slotID,
			_pDst->GetGUID(), _pDstSlot->slotID)) return;

		m_pAsset->SetDirty();

		// まとまりの見せているピンへ引かれたなら、隠している段へも配り直す
		if (const CompositeGroup* _pGroup = m_compositeGroups.Find(_pDst->GetGUID()))
		{
			m_pendingSyncGroup = _pGroup->guid;
		}
	}

	void RenderingPipelineEditor::HandleDeleteSelection()
	{
		if (!IsDeleteKeyPressed()) return;

		RenderGraph* _pGraph = m_pAsset->RefRenderGraph();

		// 選択中の線を削除
		const std::vector<int> _linkIDs = GetSelectedLinkIDs();
		if (!_linkIDs.empty())
		{
			for (int _linkID : _linkIDs)
			{
				_pGraph->RemoveLink(_linkID);
			}
			ImNodes::ClearLinkSelection();
			_pGraph->ApplyLinks();
			m_pAsset->SetDirty();
		}

		// 選択中のパスを削除(出入りする線も RemovePass 側で巻き添え削除)
		const std::vector<int> _nodeIDs = GetSelectedNodeIDs();
		if (_nodeIDs.empty()) return;

		// nodeID -> GUID をここで引いておく(消しながら引くと参照が切れる)
		std::vector<Core::GUID> _targets;
		_targets.reserve(_nodeIDs.size());
		for (int _nodeID : _nodeIDs)
		{
			Pass* _pPass = _pGraph->FindPassByNodeID(_nodeID);
			if (!_pPass) continue;

			// 出口は常駐なので Delete キーでも消さない
			if (m_pAsset->IsFinalPass(*_pPass)) continue;

			// まとまりを消すときは中のパスも道連れにする。
			// 代表だけ消すと、見えないノードがグラフに残る
			if (const CompositeGroup* _pGroup = m_compositeGroups.Find(_pPass->GetGUID()))
			{
				for (Pass* _pMember : _pGroup->members)
				{
					if (_pMember) _targets.push_back(_pMember->GetGUID());
				}
				continue;
			}

			_targets.push_back(_pPass->GetGUID());
		}
		for (const Core::GUID& _guid : _targets)
		{
			_pGraph->RemovePass(_guid);
		}
		ImNodes::ClearNodeSelection();
		m_pAsset->SetDirty();
	}

	void RenderingPipelineEditor::HandlePendingDeletePass()
	{
		if (!m_pendingDeletePass.IsValid()) return;

		m_pAsset->RefRenderGraph()->RemovePass(m_pendingDeletePass);
		m_pendingDeletePass = {};
		m_pAsset->SetDirty();
	}

	// パスの生成そのものは RenderGraph の仕事。
	// ここは、決まったノード座標を ImNodes 側へ反映するところだけを受け持つ
	void RenderingPipelineEditor::AddPassFromEditor(ID<Pass> a_typeID)
	{
		PassMetaRegistry* _pRegistry = m_pAsset->RefMetaRegistry();
		if (!_pRegistry) return;

		Pass* _pPass = m_pAsset->RefRenderGraph()->AddPass(*_pRegistry, a_typeID);
		if (!_pPass) return;

		m_pAsset->SetDirty();

		// まだ描いていないノードでも ImNodes 側は FindOrCreate なので座標だけ先に置ける。
		// 逆に ImNodes::SelectNode は描画前だと ObjectPool に無くて assert するので呼ばないこと
		const Math::Vector2& _pos = _pPass->GetEditorPos();
		ImNodes::SetNodeEditorSpacePos(_pPass->GetNodeID(), ImVec2(_pos.x, _pos.y));
	}

	//======================================================================================
	//
	// ノード座標
	//
	//======================================================================================
	void RenderingPipelineEditor::OnApplyNodePositions()
	{
		if (!m_pAsset) return;

		RenderGraph* _pGraph = m_pAsset->RefRenderGraph();
		if (!_pGraph) return;

		// まとまりは代表のノードしか出していないので、そちらだけ動かす
		const CompositeGroupTable _table = BuildCompositeGroups(*_pGraph);

		for (auto& _upPass : _pGraph->GetPasses())
		{
			if (!_upPass) continue;

			const CompositeGroup* _pGroup = _table.Find(_upPass->GetGUID());
			if (_pGroup && _pGroup->GetHead() != _upPass.get()) continue;

			const Math::Vector2& _pos = _upPass->GetEditorPos();
			ImNodes::SetNodeEditorSpacePos(_upPass->GetNodeID(), ImVec2(_pos.x, _pos.y));
		}
	}

	void RenderingPipelineEditor::OnSyncNodePositions()
	{
		if (!m_pAsset) return;

		RenderGraph* _pGraph = m_pAsset->RefRenderGraph();
		if (!_pGraph) return;

		// 出していないノードの座標を読むと原点が返るので、代表だけ書き戻す
		const CompositeGroupTable _table = BuildCompositeGroups(*_pGraph);

		for (auto& _upPass : _pGraph->GetPasses())
		{
			if (!_upPass) continue;

			const CompositeGroup* _pGroup = _table.Find(_upPass->GetGUID());
			if (_pGroup && _pGroup->GetHead() != _upPass.get()) continue;

			ImVec2 _pos = ImNodes::GetNodeEditorSpacePos(_upPass->GetNodeID());
			_upPass->SetEditorPos(Math::Vector2(_pos.x, _pos.y));
		}
	}
}
