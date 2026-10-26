#pragma once

namespace Engine::Resource
{
	class ResourceManager;
	struct AssetProperty;
}

namespace Engine::ECS
{
	class World;
}

namespace Editor::Build
{
	//==========================================================================================
	// コンバートの対象になった1件ぶんの情報
	//==========================================================================================
	struct AssetConvertContext
	{
		Engine::Resource::ResourceManager& resourceManager;
		const Engine::Resource::AssetProperty& prop;

		std::string filePath;		// アセットデータベースが返すファイルパス(拡張子つき)
		std::string fileDir;		// フォルダ
		std::string fileName;		// 拡張子なしのファイル名

		// プレハブのようにコンポーネントのメタ情報を引くワールドが要るものが使う(無ければ nullptr)
		Engine::ECS::World* pWorld = nullptr;
	};

	//==========================================================================================
	// アセットのコンバーター
	//
	// アセット種別(AssetProperty::type)ごとに1つ作る。
	// 「どのファイルを出すか」と「どう作るか」だけを持ち、一覧・ボタン・ログは BuildPanel が受け持つ。
	//
	// ・変換 = 開発用の形式(.oj* / .png / .gltf など)から、製品が読む形式(.ob* / .dds など)を書き出すこと
	// ・書き出しは各アセットが元から持つ保存処理(Save / Archive)を通す。ここで形式を組み立て直さない
	// ・変換の要らない種別(.ttf や .wav など、そのまま読むもの)は GetOutputExtension が空
	//==========================================================================================
	class IAssetConverter
	{
	public:

		virtual ~IAssetConverter() = default;

		// 受け持つアセット種別(AssetProperty::type)
		virtual const char* GetType() const = 0;

		// 変換後に残るファイルの拡張子(".obmesh" など)。変換が要らない種別は空
		virtual std::string GetOutputExtension() const = 0;

		// 変換する。失敗したら false にして a_outError に理由を入れる
		virtual bool Convert(AssetConvertContext& a_context, std::string& a_outError) const = 0;

		// 変換済みか : 出力ファイルが実際にあるかで見る(アセットデータベースの拡張子一覧は古いことがある)
		bool IsConverted(const Engine::Resource::AssetProperty& a_prop) const;

		// 変換済みでも、開発用の JSON(.oj*)の方が新しいか : 手で直した後に保存し直していないもの。
		// 重いデータ(モデル・メッシュ・アニメ)は開発中もバイナリを読むので見ない
		bool IsOutdated(const Engine::Resource::AssetProperty& a_prop) const;
	};
}
