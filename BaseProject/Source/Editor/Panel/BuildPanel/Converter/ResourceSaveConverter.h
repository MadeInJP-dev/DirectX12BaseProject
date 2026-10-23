#pragma once

#include "IAssetConverter.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

namespace Editor::Build
{
	//==========================================================================================
	// リソースマネージャーで読み込み、そのリソースの保存処理を呼ぶコンバーター
	//
	// 保存処理は .oj* と .ob* を一緒に書くので、読み込んで保存し直すだけで製品用の形式が揃う。
	// 派生は「保存の呼び方」だけを書く
	//==========================================================================================
	template<typename T>
	class ResourceSaveConverter : public IAssetConverter
	{
	public:

		ResourceSaveConverter(const char* a_type, const char* a_outputExtension)
			: m_type(a_type), m_outputExtension(a_outputExtension)
		{}

		const char* GetType() const override { return m_type; }
		std::string GetOutputExtension() const override { return m_outputExtension; }

		bool Convert(AssetConvertContext& a_context, std::string& a_outError) const final
		{
			auto _ref = a_context.resourceManager.LoadImmediate<T>(a_context.prop.guid);
			T* _pResource = a_context.resourceManager.Ref(_ref);
			if (!_pResource)
			{
				a_outError = "読み込めませんでした";
				return false;
			}

			return Save(*_pResource, a_context, a_outError);
		}

	protected:

		// 読み込んだリソースを保存する(.ob* も一緒に書かれること)
		virtual bool Save(T& a_resource, const AssetConvertContext& a_context, std::string& a_outError) const = 0;

	private:

		const char* m_type;
		const char* m_outputExtension;
	};
}
