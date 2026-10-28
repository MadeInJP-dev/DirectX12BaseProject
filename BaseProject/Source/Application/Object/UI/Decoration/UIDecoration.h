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
	// UIBase が1つ持ち、コントローラー用の組を持つ UI はもう1つを optional で持つ。
	//
	// 持ち主(UIBase)がやること :
	//   ・毎フレーム Update(親の状態を入れて)と Draw(親のトランスフォーム・設定・差し替えを入れて)を呼ぶ
	//   ・読み込み後とエディターでの差し替え後に RequestResources を呼ぶ
	//   ・Archive を自分の保存の中で呼ぶ
	//   ・エディターでは DrawInspector と DrawGizmo を呼ぶ
	//   ・当たり判定は、判定を受ける UI(UIBase::IsHitTarget)が IsHit で問い合わせる
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

		/// <summary>
		/// エレメントを1つだけ描く : 渡すものは Draw と同じ
		/// </summary>
		/// <remarks>エレメントごとに違う差し替えを掛けたいとき用(ゲージの中身だけ横に縮める等)</remarks>
		void DrawElement(size_t a_index, const DrawContext& a_context);

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
		// 当たり判定
		//-----------------------------------------------------------------------

		/// <summary>
		/// どれかのエレメントのボディに当たっているか : 当たったかどうかだけを返す
		/// </summary>
		/// <remarks>
		/// 判定を受けるか(UIBase::IsHitTarget)・切り抜きの外か は UI 側で見てから呼ぶこと。
		/// デコレーション全体を隠しているときは当たらない
		/// </remarks>
		/// <param name="a_pos">調べる位置(画面のピクセル座標。マウスの位置など)</param>
		/// <param name="a_parentTr">親(UI)の画面での置き場所(Draw へ渡すものと同じ)</param>
		/// <param name="a_parentOp">親の設定(Draw へ渡すものと同じ)</param>
		bool IsHit(const Math::Vector2& a_pos, const ParentTransform& a_parentTr, const ParentOption& a_parentOp) const;

		// ボディを持つエレメントがあるか(判定を受ける UI なのに当たる場所が無い、を見つける用)
		bool HasHitBody() const;

		/// <summary>
		/// 矩形のエレメントが占めている範囲(親のピボットからの相対, px, 親の倍率を掛ける前)
		/// </summary>
		/// <param name="a_isIncludeAnim">アニメーション・反応で変わった今の大きさも含めるか(素の大きさとの合併)</param>
		/// <returns>大きさを持つ矩形のエレメントが1つも無ければ false</returns>
		/// <remarks>文字は大きさをフォントから組み立てるので数えない</remarks>
		bool CalcBounds(Math::Vector2& a_outCenter, Math::Vector2& a_outSize, bool a_isIncludeAnim = false) const;

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
			const ParentOption& a_parentOp,
			float a_screenW,
			float a_screenH);

		/// <summary>
		/// エレメントを1つ選ぶコンボ(他の部品がエレメントを番号で指すとき用)
		/// </summary>
		/// <param name="a_inoutId">選んでいるエレメントの番号(0 で未選択)</param>
		/// <param name="a_opFilterType">この種類だけを並べる(nullopt なら全部)</param>
		/// <returns>選び直したら true</returns>
		bool DrawElementPicker(
			const char* a_label,
			uint32_t& a_inoutId,
			std::optional<EElementType> a_opFilterType = std::nullopt) const;

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

		// 種類を指定して a_index 番目へ差し込む(後ろのものは1つずつずれる)
		UIDecorationElementBase& InsertElement(size_t a_index, EElementType a_type);

		// 写しを末尾へ足す : 番号だけはここで振り直す(他の UI から移すとき用)
		UIDecorationElementBase& AddElementCopy(const UIDecorationElementBase& a_source);

		/// <summary>
		/// 旧 Decoration の配列から作り直す(今あるエレメントは捨てる)
		/// </summary>
		/// <remarks>番号は旧の番号をそのまま使う(他から番号で指されているため)。0 や重なりは振り直す</remarks>
		void AssignFromDecorations(const std::vector<Decoration>& a_decorationVec);

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

		/// <summary>
		/// 新しく振る番号の下限 : これより大きい番号だけを振る(保存しない)
		/// </summary>
		/// <remarks>
		/// 同じ UI の別の組(コントローラー用)と番号を共有するとき、相手の最大の番号を入れておく。
		/// 片方の組にだけ足したエレメントが、もう片方の別のエレメントと同じ番号にならないように
		/// </remarks>
		void SetIdFloor(uint32_t a_floor) { m_idFloor = a_floor; }

		// 今ある番号の最大(無ければ 0)
		uint32_t GetMaxElementId() const;

	private:

		// 種類からエレメントを作る
		static std::unique_ptr<UIDecorationElementBase> CreateElement(EElementType a_type);

		// エレメントへ配る描画の値 : 全体の倍率・マテリアルを足し込む
		DrawContext MakeElementContext(const DrawContext& a_context) const;

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

		// 新しく振る番号の下限(SetIdFloor)。保存しない
		uint32_t m_idFloor = 0;

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
