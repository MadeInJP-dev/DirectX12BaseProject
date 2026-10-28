#pragma once

#include "DecorationElement/UIDecorationElementBase.h"

namespace Engine::GameObject { struct ObjectGizmoContext; }

namespace App::Object::Decoration
{
	struct Decoration;

	//==========================================================================================
	// UIのデコレーション
	//
	// UI 1つに付く飾り(エレメント)の配列と、飾り全体へ掛かる設定(表示・倍率・マテリアル)をまとめたもの。
	// 今は UIBase が直接持っている飾りの配列(std::vector<Decoration>)を、これ1つで置き換える。
	//
	// 持ち主(UIBase)がやること :
	//   ・毎フレーム Update(親の状態を入れて)と Draw(親のトランスフォーム・設定・差し替えを入れて)を呼ぶ
	//   ・読み込み後とエディターでの差し替え後に RequestResources を呼ぶ
	//   ・Archive を自分の保存の中で呼ぶ
	//   ・エディターでは DrawInspector と DrawGizmo を呼ぶ
	//
	// プリセット(UIPreset)は旧 Decoration の形で保存されているので、
	// 写すときと書き戻すときは旧 Decoration へ変換して、旧の仕組み(DecorationPreset.h)をそのまま通す。
	//
	// エレメントの番号(ElementID)はここが振る。名前は書き換えられるので、他から指すときは番号を使う
	//==========================================================================================
	class UIDecoration
	{
	public:

		UIDecoration() = default;
		~UIDecoration() = default;

		// 複製はエレメントごと深く写す(コントローラー用の組を既定の組から作るときなど)
		UIDecoration(const UIDecoration& a_other);
		UIDecoration& operator=(const UIDecoration& a_other);
		UIDecoration(UIDecoration&&) noexcept = default;
		UIDecoration& operator=(UIDecoration&&) noexcept = default;

		//-----------------------------------------------------------------------
		// メイン処理
		//-----------------------------------------------------------------------

		// 更新 : アニメーションと反応を進める。出していないフレームも呼ぶこと
		void Update(const UpdateContext& a_context);

		/// <summary>
		/// 描画 : 配列の順に積む(後ろにあるものほど手前に出る)
		/// </summary>
		/// <remarks>
		/// 自分の倍率を親の倍率へ掛け、マテリアルが渡されていなければ自分のものを入れてから配る
		/// </remarks>
		void Draw(const DrawContext& a_context);

		// 保存されているGUIDから、マテリアルとエレメントのテクスチャ・フォントの参照を引き直す
		void RequestResources(Engine::Resource::ResourceManager* a_pResourceManager);

		/// <summary>
		/// アーカイブ
		/// </summary>
		/// <remarks>エレメント1つごとに区切るので、エレメントへフィールドを足しても後ろの読み出しがずれない</remarks>
		void Archive(Engine::Persistence::Archive& a_ar);

		/// <summary>
		/// プリセットが変わったエレメントへ写し直す
		/// </summary>
		/// <remarks>
		/// Draw の頭でも呼ぶので、毎フレーム呼ばなくてもよい(変わっていなければ番号を見るだけ)。
		/// 見た目ごと種類(矩形⇔文字)が変わったときは、同じ番号のまま作り直す
		/// </remarks>
		void SyncPresets(Engine::Resource::ResourceManager* a_pResourceManager);

		//-----------------------------------------------------------------------
		// エディター
		//-----------------------------------------------------------------------

		/// <summary>
		/// インスペクター : 全体の設定・エレメントの一覧(追加・削除・並べ替え)・開いているエレメントの中身
		/// </summary>
		/// <returns>値が変わったら true</returns>
		bool DrawInspector(const Engine::ECS::EngineServices& a_services);

		/// <summary>
		/// ギズモ : 開いているエレメントの位置をシーンビュー上のハンドルで動かす
		/// </summary>
		/// <param name="a_parentTr">親(UI)の画面での置き場所(Draw へ渡すものと同じ)</param>
		/// <param name="a_screenW">ゲーム画面の幅(px)</param>
		/// <param name="a_screenH">ゲーム画面の高さ(px)</param>
		/// <returns>ハンドルを動かしていたら true</returns>
		bool DrawGizmo(
			const Engine::GameObject::ObjectGizmoContext& a_ctx,
			const ParentTransform& a_parentTr,
			float a_screenW,
			float a_screenH);

		// インスペクターで開いているエレメント(無ければ無効な番号)
		ElementID GetEditElementId() const { return m_editElementId; }
		void SetEditElementId(ElementID a_id) { m_editElementId = a_id; }

		//-----------------------------------------------------------------------
		// エレメントの操作
		//-----------------------------------------------------------------------

		/// <summary>
		/// エレメントを末尾(最前面)へ足す : 番号と名前の既定値はここで入れる
		/// </summary>
		template<class T>
		T& AddElement();

		// 種類を指定して足す(エディター・読み込み用)
		UIDecorationElementBase& AddElement(EElementType a_type);

		// 消す : 見つからなければ false
		bool RemoveElement(ElementID a_id);

		/// <summary>
		/// 並べ替え : a_from 番目を a_to 番目へ動かす(描画順が変わる)
		/// </summary>
		/// <returns>範囲外なら false</returns>
		bool MoveElement(size_t a_from, size_t a_to);

		// 全部消す
		void ClearElements() { m_upElements.clear(); }

		// 探す : 見つからなければ nullptr
		UIDecorationElementBase* FindElement(const std::string& a_name);
		const UIDecorationElementBase* FindElement(const std::string& a_name) const;
		UIDecorationElementBase* FindElementById(ElementID a_id);
		const UIDecorationElementBase* FindElementById(ElementID a_id) const;

		// 型を指定して探す : 種類が違えば nullptr(RTTI を切っているので種類の値で見分ける)
		template<class T>
		T* FindElementAs(ElementID a_id)
		{
			UIDecorationElementBase* _pElement = FindElementById(a_id);
			return (_pElement && _pElement->GetType() == T::TYPE) ? static_cast<T*>(_pElement) : nullptr;
		}

		// 番号で並びを引く : 見つからなければ -1
		int FindElementIndex(ElementID a_id) const;

		// 並び順で引く
		size_t GetElementCount() const { return m_upElements.size(); }
		UIDecorationElementBase& RefElement(size_t a_index) { return *m_upElements[a_index]; }
		const UIDecorationElementBase& GetElement(size_t a_index) const { return *m_upElements[a_index]; }

		//-----------------------------------------------------------------------
		// アクセサ
		//-----------------------------------------------------------------------
		bool IsVisible() const { return m_isVisible; }
		void SetVisible(bool a_isVisible) { m_isVisible = a_isVisible; }

		float GetScale() const { return m_scale; }			// スケール取得
		void SetScale(float a_scale) { m_scale = a_scale; }

		// マテリアル : 差し替えたら参照も引き直す
		const Core::GUID& GetMaterialGUID() const { return m_materialGUID; }
		void SetMaterialGUID(const Core::GUID& a_guid, Engine::Resource::ResourceManager* a_pResourceManager);

		// 読み終わったマテリアル : 未指定・読み込み中なら nullptr
		const Engine::Resource::UIMaterial* FindReadyMaterial(Engine::Resource::ResourceManager* a_pResourceManager) const;

	private:

		// 種類からエレメントを作る
		static std::unique_ptr<UIDecorationElementBase> CreateElement(EElementType a_type);

		// 足したエレメントへ番号と名前の既定値を入れる
		void SetupAddedElement(UIDecorationElementBase& a_element);

		// 今ある番号の最大の次 : 消した番号は使い回さない
		ElementID MakeNewElementId() const;

		// 番号が無い・重なっているエレメントへ振り直す(読み込みの後)
		void AssignElementIds();

		// マテリアルの参照を引き直す
		void RequestMaterial(Engine::Resource::ResourceManager* a_pResourceManager);

		//-----------------------------------------------------------------------
		// プリセット : 旧 Decoration との変換(実装は UIDecorationPreset.cpp)
		//-----------------------------------------------------------------------

		// エレメントを旧 Decoration の形にする(プリセットに入る部分とつながりだけ)
		static Decoration ToDecoration(const UIDecorationElementBase& a_element);

		// 旧 Decoration の値をエレメントへ戻す(種類が合わない中身は読まない)
		static void ApplyDecoration(UIDecorationElementBase& a_element, const Decoration& a_decoration);

		// 旧 Decoration の種類に合うエレメントの種類
		static EElementType ToElementType(const Decoration& a_decoration);

		// プリセットが写し直しを待っているか : 届いていて番号が進んでいれば true(読み込みを頼む前も true)
		static bool IsPresetPending(const PresetLink& a_link, Engine::Resource::ResourceManager* a_pResourceManager);

		// プリセットのつながりのインスペクター : つながりが変わったら true
		static bool DrawElementPresetInspector(UIDecorationElementBase& a_element, const Engine::ECS::EngineServices& a_services);

		// 付いていく部分をプリセットへ書き戻す
		static void PushElementToPreset(const UIDecorationElementBase& a_element, Engine::Resource::ResourceManager* a_pResourceManager);

	private:
		//-----------------------------------------------------------------------
		// デコレーション本体 : 配列の後ろが最前面になる
		//-----------------------------------------------------------------------
		std::vector<std::unique_ptr<UIDecorationElementBase>> m_upElements = {};

		//-----------------------------------------------------------------------
		// デコレーション全体へ掛かる設定
		//-----------------------------------------------------------------------
		bool m_isVisible = true;	// 表示切替
		float m_scale = 1.0f;		// スケール : 親の倍率へ乗算

		// 飾り全体に掛ける演出
		Core::GUID m_materialGUID = {};
		Engine::ResourceRef<Engine::Resource::UIMaterial> m_materialRef = {};

		//-----------------------------------------------------------------------
		// エディター(保存しない)
		//-----------------------------------------------------------------------
		ElementID m_editElementId = {};		// インスペクターで開いているエレメント

	};

	//==========================================================================================
	// テンプレートの実装
	//==========================================================================================
	template<class T>
	T& UIDecoration::AddElement()
	{
		static_assert(std::is_base_of_v<UIDecorationElementBase, T>, "T は UIDecorationElementBase の派生であること");

		auto _upElement = std::make_unique<T>();
		T& _element = *_upElement;

		// 番号は配列が伸びる前に決める(伸びた後だと、作りかけの要素を数えてしまう)
		SetupAddedElement(_element);
		m_upElements.push_back(std::move(_upElement));

		return _element;
	}
}
