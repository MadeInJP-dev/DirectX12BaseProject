#pragma once

#include "Engine/GameObject/BaseObject/BaseObject.h"

namespace App::Object
{
	class UIBase;

	//===============================================================================================
	// カーソルを乗せると開く横のメニュー
	//
	// 開き口(三本線のボタンなど)にカーソルを乗せると、メニューの入れ物(UIElement)を出す。
	// 開いている間は、カーソルが開き口かメニューの範囲の中に居れば閉じず、両方から外へ出たら閉じる。
	//
	//   Trigger … 開き口(UIButton など)。ここに乗ると開く
	//   Panel   … メニューの入れ物(UIElement)。中のボタンはヒエラルキーでこの下に置く
	//   Area    … 開いている間、カーソルが居れば閉じない範囲(Panel の下に置く背景の UIElement など)
	//
	// ・開閉だけを持つ。中のボタンが押されて何をするかは、置いた画面の進行役(HomeSequence など)が差し込む。
	// ・範囲は各 UI のアンカーの矩形(PixelSize)で見る。当たり判定の取り合いとは関係なく、
	//   メニューのボタンの上に居ても「範囲の中」として扱う。
	// ・Area はカーソルを遮る UI(BlockCursor を立てた UIElement)にしておくこと。遮らないと、
	//   開いたメニューの裏のボタン(ミッションセレクトなど)が押せてしまう。
	// ・プレイ中以外(エディター操作中)は閉じておく。配置を見るときはインスペクターの PreviewOpen を使う。
	// ・開閉は横から滑り込む/滑り出る。Panel に実行中だけのずらし(UIBase::SetRuntimeOffset)を掛け、
	//   下の UI は Panel の行列に付いて動く。置き場所(PixelPos)は開いた形のまま書き換えない。
	//===============================================================================================
	class SideMenu : public Engine::GameObject::BaseObject
	{
	public:

		// 更新処理 : カーソルの位置を見て開閉する
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		// 開いているか(滑り出ている途中も、閉じると決まっていれば false)
		bool IsOpen() const { return m_isOpen; }

		// 閉じる(メニューのボタンで別の画面へ移るときなど)。滑り出てから隠れる
		void Close() { m_isOpen = false; }

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "SideMenu"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// カーソルがその UI のアンカーの矩形の中に居るか(出ていない UI は外扱い)
		static bool IsCursorInside(Engine::GameObject::ObjectContext& a_context, const UIBase* a_pUI, const Math::Vector2& a_cursorPos);

		// 開き具合を表示と位置へ反映する : 入れ物の出し入れと、下の UI 全部のずらし
		void ApplySlide(Engine::GameObject::ObjectContext& a_context);

		// 滑る距離(px) : 指定が無ければ Area の幅
		float GetSlideDistance(Engine::GameObject::ObjectContext& a_context) const;

	private:

		//------------------------------------------------------------------------------
		// 設定(保存される)
		//------------------------------------------------------------------------------
		Core::GUID m_triggerGUID = {};		// 開き口(三本線のボタン)。ここに乗ると開く
		Core::GUID m_panelGUID = {};		// メニューの入れ物(UIElement)。開いている間だけ出す
		Core::GUID m_areaGUID = {};			// 開いている間、カーソルが居れば閉じない範囲

		float m_slideTime = 0.2f;			// 滑り込む/滑り出るのにかかる時間(秒)。0 で一瞬
		float m_slideDistance = 0.0f;		// 滑る距離(px)。0 なら Area の幅(画面の外から出てくる)

		//------------------------------------------------------------------------------
		// 状態(保存しない)
		//------------------------------------------------------------------------------
		bool m_isOpen = false;			// 開いているか(目指している側)
		float m_openRate = 0.0f;		// 開き具合(0 で隠れている、1 で出きっている)
		bool m_isPreviewOpen = false;	// エディターで配置を見るために開いておくか
	};
}
