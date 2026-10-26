#pragma once

// CreateMetaData() の戻り値の型を名乗るだけなので前方宣言で足りる
#include "Core/JSONHelper/JSONForward.h"
#include "AssetTypes.h"
namespace Engine::Resource
{
	// アセットグループ : タイプごとに分けるプロパティを作る中間素材
	// 初回にできたものを基準として集める
	struct AssetGroup
	{
		std::string					type = "";			// タイプ : 初回のものを入れる
		std::string					basePath = "";		// 拡張子なしのベースパス(実際の綴り)
		std::string					fileName = "";		// ファイル名
		std::vector<std::string>	extensions = {};	// そのベースパスに付いている拡張子
	};

	// ゲームに使用する外部アセットを管理する
	class AssetDatabase
	{
	public:

		AssetDatabase() = default;
		~AssetDatabase() = default;
		NON_COPYABLE_NON_MOVABLE(AssetDatabase);

		//-----------------------------------------------------------------------------------------------------
		// 初期化
		//-----------------------------------------------------------------------------------------------------

		// アセットの上位フォルダと作成拡張子指定
		// a_isWatch : 起動中のファイルの出し入れを監視するか(しなければ起動時のものだけ)
		void Init(
			const std::string& a_assetFilePath,
			const std::string& a_metafileExtension,
			bool a_isWatch = true
		);
		// 解放処理
		void Release();

		// 読み込みたい拡張子があれば追加
		void AddSupporedExtensions(const TypeExtension& a_data);

		// アセットフォルダ以下を検索して、すべてのアセットにメタファイルを作る
		void CreateMetaFileForAllAssets();

		// ランタイム用情報へと変換
		void CreateRuntimeData();

		//-----------------------------------------------------------------------------------------------------
		// 更新
		//-----------------------------------------------------------------------------------------------------

		// クロールディレクトリ以下を監視する。ランタイム中で行うが、エディターモードのみで実行すべき
		void Update();

		// すべてのメタファイルを削除して再構築する
		void RebuildAllMetaData();		

		//-------------------------------------------------------------------------------------------------
		// メタファイルを先回りして作り、GUIDを発行する
		//
		// 普通のアセットの新規作成では呼ばないこと。
		// ファイルを置けば Update() の監視が拾ってメタファイルもGUIDも用意する。
		//
		// ここを通すのは「書き出す中身にGUIDが要る」ものだけ。
		//   ・モデル変換 : サブアセット(マテリアル/メッシュ/アニメ)のGUIDを .mdl 本体へ書く
		//   ・シーン新規作成 : 発行したGUIDでそのまま開く
		// 監視は別スレッドなのでGUIDが決まるのが翌フレームになり、これらには間に合わない
		//-------------------------------------------------------------------------------------------------
		Core::GUID AddMetaData(const std::string& a_newFilePath,const std::string& a_type);

		// データベース内にGUIDが存在するかチェック
		bool IsValid(const Core::GUID& a_guid) const;

		// ---- アクセサ ----
		std::string GetFilePathFromGUID(const std::string& a_guid) const;		// GUIDから現在のファイルパスを取得
		std::string GetFilePathFromGUID(const Core::GUID& a_guid) const;		// GUIDから現在のファイルパスを取得
		std::string GetBaseFilePathFromGUID(const Core::GUID& a_guid) const;	// ベースファイルパスの取得
		std::string GetFileNameFromGUID(const Core::GUID& a_guid) const;		// ファイルネームの取得
		Core::GUID GetGUIDFromFilePath(const std::string& a_path) const;		// ファイルパスからGUIDを取得
		const AssetNode& GetAssetRootNode() const { return m_assetRootNode; }		// アセット構造取得
		const std::unordered_map<std::string, TypeExtension>& GetAssetTypeExtensionsMap() const;
		std::span<const AssetProperty> GetTypeMetaVec(const std::string& a_type) const;		// 指定したタイプのメタ配列取得

		const AssetProperty* GetAssetProperty(const Core::GUID& a_guid) const;
		const AssetProperty* GetAssetProperty(const std::string& a_filePath) const;

		/// <summary>
		/// GUIDからメタ情報を引く。見つからなくてもログを出さない
		/// </summary>
		/// <remarks>
		/// GetAssetProperty は見つからないとログを出すので、エディターのように
		/// 毎フレーム引く場所では使えない(参照が1つ切れているだけでログが埋まる)。
		/// 「引けたら出す・駄目なら出さない」を判断するためのもの。
		/// 返るのは m_assetMap の実体なので、アセットパネルが配っているものと同じポインタ。
		/// </remarks>
		AssetProperty* FindAssetProperty(const Core::GUID& a_guid);
	private:

		//-----------------------------------------------------------------------------------------------------
		// ロックを取らない実装
		//
		// m_mutex は std::mutex なので同じスレッドから二重に取れない。
		// ロック済みの関数から公開関数を呼ぶと自分の解放を待って固まるため、
		// 処理をまたぐときは公開関数ではなくこちらを呼ぶこと。
		// 呼ぶ側が m_mutex を取っていることが前提。
		//-----------------------------------------------------------------------------------------------------

		// アセットフォルダ以下を検索して、すべてのアセットにメタファイルを作る
		void CreateMetaFileForAllAssetsInternal();

		// ランタイム用情報へと変換
		void CreateRuntimeDataInternal();

		// 別スレッドでファイルの変更を監視する関数
		void FileWatch();

		// 新たにメタファイルの内容を作成して返す
		nlohmann::json CreateMetaData(const std::filesystem::path& a_srcFile);

		// 管理対象拡張子かどうか調べる
		bool IsSupported(const std::filesystem::path& a_filepath);

		// アセットツリーの更新
		void RefreshAssetTree();

		// ファイル単体でのプロパティ操作
		void AddFileProperty(const std::filesystem::path& a_filePath);
		void RemoveFileProperty(const std::filesystem::path& a_filePath);
		void ChangeFileName(const std::filesystem::path& a_filePath);

		// ファイルパスの拡張子からアセットのタイプを検出
		std::string GetAssetType(const std::filesystem::path& a_filePath) const;

	private:

		// ファイルパス
		std::string m_assetsFilePath = {};		// アセットが入っているフォルダ
		std::string m_metafileExtension = {};	// 作成するメタファイルの拡張子

		// 管理しているタイプと拡張子
		std::unordered_map<std::string, TypeExtension> m_assetTypeExtensionsMap;
		// 対応しているファイル拡張子ごとのメタデータ
		std::unordered_map<std::string, std::vector<AssetProperty>> m_typeMetaMap = {};
		// 管理しているすべてのアセットメタデータ
		std::unordered_map<Core::GUID, AssetProperty> m_assetMap;

		// 階層構造
		AssetNode m_assetRootNode = {};

		// ランタイム中のディレクトリ管理
		std::thread m_fileWatcherThread;
		std::atomic_bool m_isWatching = false;
		std::mutex m_mutex;

		std::atomic_bool m_isDirtyDir = false;		// ディレクトリ以下で変更があったかどうか
		HANDLE m_dirHandle;				// ディレクトリハンドル

		// 中間バッファ
		std::array<std::byte, 64 * 1024> m_buffer;											// 変更点
		std::unordered_map<std::string, std::vector<AssetGroup>> m_typeAssetGroupTemp = {};	// 変更をためるバッファ
		std::unordered_map<Core::GUID, AssetProperty> m_changedAssetPropMap = { };		// 既存に対しての変更点
	};
}