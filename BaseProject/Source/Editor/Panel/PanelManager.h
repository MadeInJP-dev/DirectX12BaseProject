#pragma once

#include "../Internal/EditorContext.h"
#include "../Panel/IPanel.h"
#include "Editor/EditorCommon.h"

namespace Editor
{
	class EditorCamera;
	class Profiler;
	class EffectEditor;
	class PrefabEditor;

	/// <summary>
	/// パネルを管理するためのクラス
	/// </summary>
	class PanelManager
	{
	public:

		/// <summary>
		/// パネルの登録
		/// </summary>
		void Init(EditorCamera* a_pEditorCamera, EffectEditor* a_pEffectEditor, PrefabEditor* a_pPrefabEditor, Profiler* a_pProfiler, ECS::EngineServices* a_pServices);

		/// <summary>
		/// パネルの描画
		/// </summary>
		void OnDrawPanels();

		/// <summary>
		/// シーンに紐づく選択状態を捨てる(シーン切り替え時に呼ぶ)
		/// </summary>
		void ClearSceneContext();

		/// <summary>
		/// パネルの登録
		/// </summary>
		/// <typeparam name="T">型</typeparam>
		template<typename T>
		void RegisterPanel();

		/// <summary>
		/// 登録済みパネルの取得
		/// パネルは登録後に増減しないので、返したポインタは保持してよい
		/// </summary>
		/// <typeparam name="T">取得したいパネルの型</typeparam>
		/// <returns>見つからなければ nullptr</returns>
		template<typename T>
		T* RefPanel();

	private:

		/// <summary>
		/// 選択中のものが今のシーンにまだ在るかを確かめ、無ければ選択を外す。
		///
		/// パネル側にも同じ検証はあるが、あちらは ImGui::Begin が false
		/// (畳まれている・タブが裏)だと呼ばれないので当てにできない。
		/// 描画の前にここで一度だけ通す
		/// </summary>
		void ValidateContext();

	private:

		// 描画パネル配列
		std::vector<std::unique_ptr<IPanel>> m_upPanelVec = {};

		// 各パネルの型(m_upPanelVec と同じ並び)。RefPanel で型から引くのに使う
		std::vector<TypeInfo::TypeKey> m_panelTypeKeyVec = {};

		// パネル間共通メモ帳
		EditorContext m_editContext = {};
	};

	template<typename T>
	inline void PanelManager::RegisterPanel()
	{
		m_upPanelVec.push_back(std::make_unique<T>());
		m_panelTypeKeyVec.push_back(TypeInfo::GetTypeKey<T>());
	}

	template<typename T>
	inline T* PanelManager::RefPanel()
	{
		// 登録した型そのもので引く(基底の型では引けない)
		const TypeInfo::TypeKey _key = TypeInfo::GetTypeKey<T>();
		for (size_t _i = 0; _i < m_upPanelVec.size(); ++_i)
		{
			if (m_panelTypeKeyVec[_i] == _key)
			{
				return static_cast<T*>(m_upPanelVec[_i].get());
			}
		}
		return nullptr;
	}
}