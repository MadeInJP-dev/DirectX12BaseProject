#pragma once

#include "Editor/EditorCommon.h"

//==========================================================================================
//
// RenderingPipelineEditor (Editor::Inspector)
//
// レンダリングパイプラインアセットの編集UI。
//
// 以前は RenderingPipelineAsset がこの中身をまるごと持っていたが、
// ランタイム側のアセットが ImNodes とエディターの選択状態を抱える形になっていた。
// ここへ移して、アセット側は「グラフを1つ持つ」だけに戻してある。
//
// ImNodes の作法は NodeGraphEditor が持つので、ここは何を描くかだけを書く。
//
// アセットは GUID で持って毎フレーム引き直す。
// 実体ポインタを抱えると、アンロードや作り直しで切れる
//
//==========================================================================================
#include "../../../../../ImGui/ImNode/Core/NodeGraphEditor.h"
#include "PassEditor/PassEditor.h"
#include "CompositeNode/CompositeNode.h"

namespace Engine::Graphics::Pipeline
{
	class Pass;
	class RenderingPipelineAsset;
}

namespace Editor::Inspector
{
	class RenderingPipelineEditor : public NodeGraphEditor
	{
	public:

		explicit RenderingPipelineEditor(const Core::GUID& a_assetGUID)
			: m_assetGUID(a_assetGUID)
		{
			// パスの種類ごとの編集UIを揃える。
			// 実体は型ごとに1つで状態を持たないので、開くたびに作り直して構わない
			RegisterBuiltinPassEditors(m_passEditorRegistry);
			RegisterBuiltinCompositeNodes(m_compositeNodeRegistry);

			InitStandardConfirm();
		}

		~RenderingPipelineEditor() override = default;

		// このエディターが開いているアセット。
		// 選択が変わったかどうかの判定に使う
		const Core::GUID& GetAssetGUID() const { return m_assetGUID; }

	protected:

		bool OnBeginDraw(EditorContext& a_editContext) override;
		void OnDrawHeader(EditorContext& a_editContext) override;
		void OnDrawNodes(EditorContext& a_editContext) override;
		void OnPostDraw(EditorContext& a_editContext) override;

		void OnApplyNodePositions() override;
		void OnSyncNodePositions() override;

	private:

		//----------------------------------------------------------------------------------
		// ヘッダー(グラフの外)
		//----------------------------------------------------------------------------------
		void DrawToolbar(EditorContext& a_editContext);	// 保存・追加・コンパイル・既定構成
		void DrawAddPass();								// パス追加ボタン + ポップアップ
		void DrawAddComposite();						// 合成ノード追加ボタン + ポップアップ
		void DrawValidation();							// 検証結果の一覧
		void DrawSelectedPassDetail();					// 選択中パスの詳細(EditUpdate)
		void InitStandardConfirm();						// 既定構成で組み直す前の確認を用意する

		//----------------------------------------------------------------------------------
		// グラフの中
		//----------------------------------------------------------------------------------
		void DrawNode(Graphics::Pipeline::Pass& a_pass);	// ノード1つ分の枠とピン

		// まとまり1つ分のノード : 代表のパスへ、見せるピンだけを並べる
		void DrawCompositeNode(const CompositeGroup& a_group, ICompositeNode& a_node);

		// このピンをノードに出したか。
		// 出していないピンへの線は引かない(中の配線を隠すため)
		bool IsVisiblePin(int a_pinID) const { return m_visiblePinSet.contains(a_pinID); }

		//----------------------------------------------------------------------------------
		// 操作
		//----------------------------------------------------------------------------------
		void HandleCreateLink();						// 線が引かれたときの処理
		void HandleDeleteSelection();					// Delete キーでの削除
		void HandlePendingDeletePass();					// ノード内ボタンで予約された削除
		void HandlePendingRequest();					// ノードから出た要求(段数変更)を通す
		void HandlePendingSyncGroup();					// まとまりの中の配線を組み直す
		void HandlePendingApplyPos();					// 出てきたノードの座標だけを配る

		// パスを追加して、決まったノード座標を ImNodes 側へ反映する
		void AddPassFromEditor(ID<Graphics::Pipeline::Pass> a_typeID);

		// 編集対象。OnBeginDraw で引き直したものをそのフレームの間だけ持つ
		Graphics::Pipeline::RenderingPipelineAsset* m_pAsset = nullptr;

		// 開いているアセット : 同一性はこちら
		Core::GUID m_assetGUID = {};

		// このフレーム内で削除予約されたパス。
		// パス配列を回している最中に消すとイテレータが壊れるので、後でまとめて消す
		Core::GUID m_pendingDeletePass = {};

		// パスの種類ごとの編集UI。
		// 以前は Pass 自身が ImGui を呼んでいたぶんがここへ移っている
		PassEditorRegistry m_passEditorRegistry = {};

		// 「1ノード = 複数パス」のまとまりを面倒見る側
		CompositeNodeRegistry m_compositeNodeRegistry = {};

		// このフレームのまとまり : OnDrawNodes の頭で組み直す
		CompositeGroupTable m_compositeGroups = {};

		// このフレームにノードへ出したピン。
		// まとまりが隠しているピンへの線を描かないための印
		std::unordered_set<int> m_visiblePinSet = {};

		// 中の配線を組み直すまとまり : 段数を変えた直後に通す
		Core::GUID m_pendingSyncGroup = {};

		// ノードから出た「グラフを触ってほしい」という要求。
		// ノードを回している最中にパスを増減させると反復が壊れるので、
		// 受け取るだけにして描き終わってから通す
		Core::GUID m_pendingRequestGroup = {};
		CompositeNodeRequest m_pendingRequest = {};

		// 座標を配り直すパス : まとまりを解いて出てきたぶんだけ。
		// 全体を配り直すと、動かしてあったノードが保存位置へ巻き戻る
		std::vector<Core::GUID> m_pendingApplyPosVec = {};

		// 既定構成(Standard)で組み直す前の確認。今のパスと配線をすべて捨てるため
		ConfirmPopup m_standardConfirmPopup{ "Standard Pipeline" };
	};
}
