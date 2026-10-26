#include "AssetDatabase.h"

// メタファイルを組み立てるのでここでは JSON の実体を触る。
// プリコンパイル済みヘッダーへ置くと全翻訳単位に広がるため
#pragma warning(push, 0)
#include <nlohmannJSON/json.hpp>
#pragma warning(pop)

#include "Core/JSONHelper/JSONHelper.h"

namespace Engine::Resource
{
	namespace
	{
		//----------------------------------------------------------------------
		// パスを小文字化する
		//----------------------------------------------------------------------
		// Windowsのファイルシステムは大文字小文字を区別しないので、
		// 「同じファイルを指しているか」を文字列で判定したいときはこれを通す。
		// 表示や実際のファイル操作には元の綴りを使うこと。
		//----------------------------------------------------------------------
		std::string ToLowerPath(const std::string& a_path)
		{
			std::string _result = a_path;
			std::transform(_result.begin(), _result.end(), _result.begin(),
				[](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });

			return _result;
		}
	}

	void AssetDatabase::Init(
		const std::string& a_assetFilePath,
		const std::string& a_metafileExtension,
		bool a_isWatch
	)
	{
		// 初期化
		m_assetsFilePath = a_assetFilePath;
		m_metafileExtension = a_metafileExtension;

		m_dirHandle = INVALID_HANDLE_VALUE;

		// 監視しないときはスレッドも立てない(Update は変更が届かないので何もしない)
		if (!a_isWatch) return;

		// ディレクトリ以下の更新検知用にハンドルを作成
		m_dirHandle = CreateFileW(
			String::ToWideString(a_assetFilePath).c_str(),
			FILE_LIST_DIRECTORY,
			FILE_SHARE_READ |
			FILE_SHARE_WRITE |
			FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS,
			nullptr
		);

		if (m_dirHandle == INVALID_HANDLE_VALUE)
		{
			ENGINE_ERRLOG(false,"[AssetDatabase] ディレクトリ監視の開始に失敗");
			return;
		}

		m_isWatching = true;

		m_fileWatcherThread = std::thread(&AssetDatabase::FileWatch, this);
	}
	void AssetDatabase::Release()
	{
		m_isWatching = false;

		// ハンドルの解放
		if (m_dirHandle != INVALID_HANDLE_VALUE)
		{
			CancelIoEx(m_dirHandle, nullptr);
			CloseHandle(m_dirHandle);
			m_dirHandle = INVALID_HANDLE_VALUE;
		}

		// 監視スレッドの終了
		if (m_fileWatcherThread.joinable())
		{
			m_fileWatcherThread.join();
		}
	}
	void AssetDatabase::AddSupporedExtensions(const TypeExtension& a_data)
	{
		std::unique_lock _gard(m_mutex);
		m_assetTypeExtensionsMap[a_data.type] = a_data;
	}
	Core::GUID AssetDatabase::AddMetaData(const std::string& a_baseFilePath, const std::string& a_type)
	{
		std::unique_lock _gard(m_mutex);
		// 拡張子なしの論理パス
		std::filesystem::path _basePath(a_baseFilePath);				// ファイルパス
		std::filesystem::create_directories(_basePath.parent_path());	// 親フォルダを作成

		// メタファイルパス作成
		std::filesystem::path _metaPath = _basePath.string() + m_metafileExtension;

		// メタファイルの作成（すでに存在していれば既存のGUIDを使う）
		nlohmann::json _json;
		Core::GUID _guid;

		if (std::filesystem::exists(_metaPath))
		{
			// 既存のメタファイルがあればそこから取得
			std::ifstream _ifs(_metaPath.string());
			_ifs >> _json;
			_ifs.close(); // 念のためcloseを追加
			_guid.FromString(Core::JSONHelper::GetValue<std::string>("GUID", _json, Core::DEFAULT_GUID.String()));
		}
		else
		{
			// なければデータを作成して保存
			_guid.Create();
			_json["GUID"] = _guid.String();
			_json["Type"] = a_type;
			_json["Files"] = nlohmann::json::array();		// 手動追加時には空配列

			std::ofstream _metafile(_metaPath);
			_metafile << _json.dump(4);
			_metafile.close();
		}

		// -----------------------------------------------------
		// ランタイムデータの作成と更新
		// -----------------------------------------------------
		AssetProperty _prop;
		_prop.filePath = _basePath.lexically_normal().generic_string();
		_prop.fileName = _basePath.filename().string();
		// JSONからTypeを取得（なければ引数のa_type）
		_prop.type = Core::JSONHelper::GetValue<std::string>("Type", _json, a_type);
		_prop.guid = _guid;

		// アセットが持っている拡張子リストの復元（CreateRuntimeDataと同じ動き）
		if (_json.contains("Files"))
		{
			for (const auto& _ext : _json["Files"])
			{
				_prop.extensionsVec.push_back(_ext.get<std::string>());
			}
		}

		// GUIDをキーにして登録（上書き更新）
		m_assetMap[_prop.guid] = _prop;

		// m_typeMetaMap の重複登録を防ぎつつ更新
		auto& _metaVec = m_typeMetaMap[_prop.type];
		auto _it = std::find_if(_metaVec.begin(), _metaVec.end(), [&_guid](const AssetProperty& p) { return p.guid == _guid; });
		if (_it != _metaVec.end())
		{
			*_it = _prop; // 既存なら上書き
		}
		else
		{
			_metaVec.push_back(_prop); // 新規なら追加
		}

		// -----------------------------------------------------
		// ツリー階層構造の再構築
		// -----------------------------------------------------
		RefreshAssetTree();

		return _prop.guid;
	}
	bool AssetDatabase::IsValid(const Core::GUID& a_guid) const
	{
		if (m_assetMap.find(a_guid) != m_assetMap.end())
		{
			return true;
		}
		return false;
	}
	void AssetDatabase::CreateMetaFileForAllAssets()
	{
		std::unique_lock _gard(m_mutex);
		CreateMetaFileForAllAssetsInternal();
	}
	void AssetDatabase::CreateMetaFileForAllAssetsInternal()
	{
		//----------------------------------------------------------------------
		// 拡張子なしのベースパスごとに拡張子をまとめる
		//----------------------------------------------------------------------
		// キーは小文字化したベースパス。Windowsのファイルシステムは大文字小文字を
		// 区別しないので、"Sand.gltf" と "sand.png" は別のグループになるのに、
		// 書き出す .assetmeta は同じ1つのファイルを指してしまう。
		// 分けたままにすると、後から書いたグループが先のグループの拡張子リストを
		// 丸ごと上書きする(GUIDとTypeだけは既存ファイルから引き継がれるので、
		// 「Typeは Model なのに Files は .png だけ」という状態が出来上がり、
		// モデルとして読もうとしたときに拡張子が見つからず読み込みに失敗する)。
		//
		// 実際に使うベースパスの綴りは、最初に見つけたものを採用する。
		//----------------------------------------------------------------------

		std::map<std::string, AssetGroup> _assetGroups;

		// スキャンしてベース名ごとに拡張子をグループ化
		for (const std::filesystem::directory_entry& _entry : std::filesystem::recursive_directory_iterator(m_assetsFilePath))
		{
			// エントリーがファイルかつサポートされた拡張子なら
			if (_entry.is_regular_file() && IsSupported(_entry.path()))
			{
				std::filesystem::path _basePath = _entry.path().parent_path() / _entry.path().stem();
				std::string _basePathStr = _basePath.lexically_normal().generic_string();				// 拡張子なしのベースパス

				// ベースパスのグループに追加(ファイルシステムに合わせて大文字小文字を無視する)
				AssetGroup& _group = _assetGroups[ToLowerPath(_basePathStr)];
				if (_group.basePath.empty()) _group.basePath = _basePathStr;

				_group.extensions.push_back(_entry.path().extension().string());
			}
		}

		// 拡張子から所属するアセットの種別を引く(見つからなければ空)
		auto _findType = [this](const std::string& a_ext) -> std::string
		{
			for (const auto& [_typeName, _typeExtData] : m_assetTypeExtensionsMap)
			{
				// 独自規格のチェック (.ob, .oj)
				for (const auto& _tExt : _typeExtData.typeExt)
				{
					if (a_ext.find(_tExt) == 0) return _typeName;
				}

				// ベース拡張子のチェック (.gltf など)
				for (const auto& _baseExt : _typeExtData.extensions)
				{
					if (a_ext == _baseExt) return _typeName;
				}
			}

			return "";
		};

		// 全ファイルがグループに登録されたため、グループごとのメタファイルを作成
		for (const auto& [_key, _group] : _assetGroups)
		{
			const std::string&              _basePathStr = _group.basePath;
			const std::vector<std::string>& _extensions  = _group.extensions;

			//------------------------------------------------------------------
			// 種別のまたがりを知らせる
			//------------------------------------------------------------------
			// このデータベースは「拡張子を除いたパス」でアセットを1件と数えるので、
			// 同じフォルダの同じ名前に別種のファイルを置くと1件にまとめられてしまう
			// (例 : Sand.gltf と sand.png)。まとめられた側は個別のアセットとして
			// 引けなくなり、モデルのテクスチャが解決できないといった形で出る。
			// 直し方は「片方の名前を変える」しかないので、気づけるように出しておく。
			//------------------------------------------------------------------
			{
				std::string _firstType = "";
				for (const auto& _ext : _extensions)
				{
					const std::string _type = _findType(_ext);
					if (_type.empty()) continue;

					if (_firstType.empty()) { _firstType = _type; continue; }
					if (_firstType == _type) continue;

					ENGINE_WARNING(
						"[AssetDatabase] 同じ名前に別種のアセットがあります(片方の名前を変えてください) : %s (%s / %s)",
						_basePathStr.c_str(), _firstType.c_str(), _type.c_str());
					break;
				}
			}

			// 作成するメタファイルのフルパスを作成
			std::filesystem::path _metafilePath = _basePathStr + m_metafileExtension;

			// すでにメタファイルが存在する場合は、持っている拡張子リストのみ更新して保存
			nlohmann::json _json;
			if (std::filesystem::exists(_metafilePath))
			{
				std::ifstream _ifs(_metafilePath.string());
				if (_ifs.is_open())
				{
					_ifs >> _json;
					_ifs.close();
				}
			}
			else
			{
				Core::GUID _guid;
				_guid.Create();
				_json["GUID"] = _guid.String();

				// 拡張子からタイプを推測する。
				// 最初に見つかった拡張子のタイプで確定させる(=先に並んでいるものが優先)。
				// 種別ごとのループを抜けずに回し続けると、後ろの種別に一致したときに
				// 上書きされ、同じ構成でも並び順しだいで別のタイプになってしまう。
				std::string _typeStr = "Unknown";
				for (const auto& _ext : _extensions)
				{
					const std::string _type = _findType(_ext);
					if (_type.empty()) continue;

					_typeStr = _type;
					break;
				}
				_json["Type"] = _typeStr;
			}

			// 拡張子リストを常に最新の状態に更新
			auto _array = nlohmann::json::array();
			for (const auto& _ext : _extensions) { _array.push_back(_ext); }
			_json["Files"] = _array;

			std::ofstream _metafile(_metafilePath);
			_metafile << _json.dump(4);
			_metafile.close();
		}
	}

	void AssetDatabase::Update()
	{
		std::unique_lock _gard(m_mutex);

		// 変更が検知されていればランタイムデータを書き換える : メインスレッド
		if (!m_isDirtyDir) return;
		m_isDirtyDir = false;

		// 既存のプロパティに対する変更
		for (auto& [_guid, _prop] : m_changedAssetPropMap)
		{
			// 登録されている拡張子がなければすでにメタデータを必要とする実データが消えている
			if (_prop.extensionsVec.empty())
			{
				const Core::GUID _removeGUID = _guid;

				//----------------------------------------------------------------------
				// 実体が無くなったのでメタファイルも消す
				//
				// 残すと次の起動で CreateRuntimeData が実体の無いアセットを
				// そのまま復活させてしまう(あちらの実体チェックは無効化されている)。
				// 消したメタファイル自身も通知として返ってくるが、
				// .assetmeta は登録タイプに無いので GetAssetType が弾く
				//----------------------------------------------------------------------
				std::error_code _ec;
				std::filesystem::remove(_prop.filePath + m_metafileExtension, _ec);

				// タイプ別の配列からも取り除く(アセット選択の一覧に残さない)
				auto _typeIt = m_typeMetaMap.find(_prop.type);
				if (_typeIt != m_typeMetaMap.end())
				{
					std::erase_if(
						_typeIt->second,
						[&_removeGUID](const AssetProperty& a_prop) { return a_prop.guid == _removeGUID; }
					);
				}

				m_assetMap.erase(_removeGUID);
				continue;
			}
			// 配列だけでも良かったが、一応すべて上書き
			m_assetMap[_guid] = _prop;

			// メタファイルのパス
			std::filesystem::path _metaPath = _prop.filePath + m_metafileExtension;

			// メタファイルが存在している前提なので更新
			nlohmann::json _json;
			if (std::filesystem::exists(_metaPath))
			{
				std::ifstream _ifs(_metaPath.string());
				if (_ifs.is_open())
				{
					_ifs >> _json;
					_ifs.close();
				}
			}

			// 拡張子リストを常に最新の状態に更新
			auto _array = nlohmann::json::array();
			for (const auto& _ext : _prop.extensionsVec)
			{
				_array.push_back(_ext); 
			}
			_json["Files"] = _array;

			// ファイルに保存
			std::ofstream _metafile(_metaPath);
			_metafile << _json.dump(4);
			_metafile.close();
		}

		// 新規に追加
		for (auto& [_type, _groupVec] : m_typeAssetGroupTemp)
		{
			// グループごとにメタファイルを作成していく
			for (auto& _group : _groupVec)
			{
				// 作成するメタファイルのフルパスを作成
				std::filesystem::path _metaPath = _group.basePath + m_metafileExtension;

				// メタファイルの作成（すでに存在していれば既存のGUIDを使う）
				nlohmann::json _json;
				Core::GUID _guid;

				// 拡張子リスト : メタファイルへ書くものと登録するもので同じものを使う
				std::vector<std::string> _extensionsVec = {};

				// 念のため既存のメタファイルがないかチェック
				if (std::filesystem::exists(_metaPath))
				{
					// 既存のメタファイルがあればそこから取得
					std::ifstream _ifs(_metaPath.string());
					_ifs >> _json;
					_ifs.close();
					_guid.FromString(Core::JSONHelper::GetValue<std::string>("GUID", _json, Core::DEFAULT_GUID.String()));

					//--------------------------------------------------------------
					// 書いてある拡張子リストを土台にする
					//
					// アセットのフォルダを丸ごと持ち込むと .assetmeta も付いてくる。
					// 通知で拾えるのは今回届いた分だけなので、
					// 元から書いてあるものを残したうえで今回の分を足す。
					//
					// ただし実体が無いものは引き継がない。
					// GetFilePathFromGUID は .ob/.oj を優先して返すため、
					// 消えた独自データが載ったままだと存在しないパスを掴んで
					// 読み込みに失敗する(足りない分は後から届く通知で戻る)
					//--------------------------------------------------------------
					if (_json.contains("Files"))
					{
						for (const auto& _extJson : _json["Files"])
						{
							auto _ext = _extJson.get<std::string>();
							if (!std::filesystem::exists(_group.basePath + _ext)) continue;

							_extensionsVec.push_back(_ext);
						}
					}
				}

				// GUIDが引けないメタファイル(壊れている・手で作られた)なら発行し直す
				if (!_guid.IsValid())
				{
					_guid.Create();
				}
				_json["GUID"] = _guid.String();

				// タイプは書いてあるものを優先する(持ち込んだメタファイルの種別を勝手に変えない)
				if (!_json.contains("Type")) _json["Type"] = _type;

				// 今回検出した拡張子を足す(すでに載っているものは足さない)
				for (const auto& _ext : _group.extensions)
				{
					if (std::find(_extensionsVec.begin(), _extensionsVec.end(), _ext) != _extensionsVec.end()) continue;

					_extensionsVec.push_back(_ext);
				}

				//------------------------------------------------------------------
				// メタファイルへ書き戻す
				//
				// 既存のメタファイルがあった場合も必ず通すこと。
				// 新規作成のときだけ書いていると Files が古いまま残り、
				// ディスクの内容とランタイムデータが食い違う
				//------------------------------------------------------------------
				auto _array = nlohmann::json::array();
				for (const auto& _ext : _extensionsVec)
				{
					_array.push_back(_ext);
				}
				_json["Files"] = _array;

				std::ofstream _metafile(_metaPath);
				_metafile << _json.dump(4);
				_metafile.close();

				// -----------------------------------------------------
				// ランタイムデータの作成と更新
				// -----------------------------------------------------
				AssetProperty _prop;
				_prop.filePath = _group.basePath;
				_prop.fileName = _group.fileName;
				// タイプは今書き出したメタファイルに合わせる(CreateRuntimeDataと同じ引き方)
				_prop.type = Core::JSONHelper::GetValue<std::string>("Type", _json, _group.type);
				_prop.guid = _guid;
				_prop.extensionsVec = _extensionsVec;

				// GUIDをキーにして登録
				m_assetMap[_prop.guid] = _prop;

				// m_typeMetaMap の重複登録を防ぎつつ更新
				auto& _metaVec = m_typeMetaMap[_prop.type];
				auto _it = std::find_if(_metaVec.begin(), _metaVec.end(), [&_guid](const AssetProperty& p) { return p.guid == _guid; });
				if (_it != _metaVec.end())
				{
					*_it = _prop; // 既存なら上書き
				}
				else
				{
					_metaVec.push_back(_prop); // 新規なら追加
				}
			}
		}

		// -----------------------------------------------------
		// ツリー階層構造の再構築
		// -----------------------------------------------------
		RefreshAssetTree();

		m_typeAssetGroupTemp.clear();
		m_changedAssetPropMap.clear();
	}

	void AssetDatabase::RebuildAllMetaData()
	{
		std::unique_lock _gard(m_mutex);
		// ディレクトリが存在しない場合は処理をしない
		if (!std::filesystem::exists(m_assetsFilePath)) return;

		// 既存のメタファイルのみをすべて削除
		for (const auto& _entry : std::filesystem::recursive_directory_iterator(m_assetsFilePath))
		{
			// ファイルかつ拡張子がメタファイルのものだけを狙う
			if (_entry.is_regular_file() && _entry.path().extension().string() == m_metafileExtension)
			{
				std::error_code _ec;
				std::filesystem::remove(_entry.path(), _ec);

				// 万が一ファイルロック等で消せなかった場合のログ出力
				if (_ec)
				{
					ENGINE_LOG("DeleteMetaFile False : %s", _entry.path().string().c_str());
				}
			}
		}

		// メタファイルを完全にクリーンな状態から作り直す
		// ロックはこの関数で取っているので、公開関数ではなく実装側を呼ぶこと
		CreateMetaFileForAllAssetsInternal();

		// ランタイムデータを最新のものに更新
		CreateRuntimeDataInternal();

		ENGINE_LOG("All Asset Rebuild MetaData");
	}

	void AssetDatabase::CreateRuntimeData()
	{
		std::unique_lock _gard(m_mutex);
		CreateRuntimeDataInternal();
	}
	void AssetDatabase::CreateRuntimeDataInternal()
	{
		m_assetMap.clear();
		m_typeMetaMap.clear();

		// 指定フォルダ以下のメタファイルをすべて検索
		for (auto& _entry : std::filesystem::recursive_directory_iterator(m_assetsFilePath))
		{
			// 通常ファイルでない ・ メタファイルではない
			if (_entry.is_regular_file() == false) continue;
			if (_entry.path().extension().string() != m_metafileExtension) continue;

			// メタファイルの読み込み
			std::ifstream _ifs(_entry.path().string());
			if (_ifs.fail()) continue;

			nlohmann::json _json;
			try
			{
				_ifs >> _json;
			}
			catch (const nlohmann::json::parse_error&)
			{
				ENGINE_WARNING("ファイルオープンエラー : %s", _entry.path().string().c_str());
				continue;
			}

			// 現在のリソースパス（メタファイルの拡張子 .assetmeta を削除）
			auto _resPath = _entry.path();
			_resPath.replace_extension("");

			// 実体チェック
			//if (std::filesystem::exists(_resPath) == false) continue;

			// アセットプロパティの作成
			AssetProperty _property = {};

			// パスの正規化（ 区切り = / ）
			_property.filePath = _resPath.lexically_normal().generic_string();				// ファイルパス
			_property.fileName = _resPath.filename().string();								// ファイル名
			_property.type = Core::JSONHelper::GetValue<std::string>("Type", _json, "Unknown");	// タイプの取得

			// GUIDの取得
			Core::GUID _guid = {};
			std::string _default = _guid.String();
			_property.guid.FromString(Core::JSONHelper::GetValue<std::string>("GUID", _json, _default));

			// アセットが持っている拡張子リストの復元
			if (_json.contains("Files"))
			{
				for (const auto& _ext : _json["Files"])
				{
					_property.extensionsVec.push_back(_ext.get<std::string>());
				}
			}

			// GUIDをキーにして登録
			m_assetMap[_property.guid] = _property;

			// 拡張子ごとにメタ配列を作成
			m_typeMetaMap[_property.type].push_back(_property);
		}

		// 階層構造の作成
		RefreshAssetTree();
	}

	std::string AssetDatabase::GetFilePathFromGUID(const std::string& a_guid) const
	{
		Core::GUID _guid = {};
		_guid.FromString(a_guid);
		return GetFilePathFromGUID(_guid);
	}

	std::string AssetDatabase::GetFilePathFromGUID(const Core::GUID& a_guid) const
	{
		auto _it = m_assetMap.find(a_guid);
		if (_it == m_assetMap.end()) return "";

		const AssetProperty& _prop = _it->second;

		// 最適な拡張子を探して結合する (.ob > .oj > 元データの順)
		auto _typeIt = m_assetTypeExtensionsMap.find(_prop.type);
		if (_typeIt != m_assetTypeExtensionsMap.end())
		{
			const auto& _typeData = _typeIt->second;

			// 1. 独自規格 (.ob系, .oj系) があれば優先して返す
			for (const auto& _tExt : _typeData.typeExt)
			{
				for (const auto& _ext : _prop.extensionsVec)
				{
					if (_ext.find(_tExt) == 0) return _prop.filePath + _ext;
				}
			}

			// 2. なければベース拡張子 (.gltf等) を返す
			for (const auto& _ext : _prop.extensionsVec)
			{
				for (const auto& _baseExt : _typeData.extensions)
				{
					if (_ext == _baseExt) return _prop.filePath + _ext;
				}
			}
		}

		// 万が一解決できなかった場合は、持っている最初の拡張子をとりあえず付ける
		if (!_prop.extensionsVec.empty())
		{
			return _prop.filePath + _prop.extensionsVec[0];
		}

		return _prop.filePath; // 最終手段
	}

	std::string AssetDatabase::GetBaseFilePathFromGUID(const Core::GUID& a_guid) const
	{
		auto _it = m_assetMap.find(a_guid);
		if (_it != m_assetMap.end())
		{
			return _it->second.filePath;
		}

		return "";
	}

	std::string AssetDatabase::GetFileNameFromGUID(const Core::GUID& a_guid) const
	{
		auto _it = m_assetMap.find(a_guid);
		if (_it != m_assetMap.end())
		{
			return _it->second.fileName;
		}

		return "";
	}

	Core::GUID AssetDatabase::GetGUIDFromFilePath(const std::string& a_path) const
	{
		// 入力されたパスから拡張子を取り除き、ベースパスとして正規化する
		std::filesystem::path _inputPath(a_path);
		std::filesystem::path _basePath = _inputPath.parent_path() / _inputPath.stem();
		std::string _normBasePath = _basePath.lexically_normal().generic_string();

		for (auto& [_guid, _assetProp] : m_assetMap)
		{
			// 登録されているベースパスと比較
			if (_assetProp.filePath == _normBasePath)
			{
				return _guid;
			}
		}

		return Core::DEFAULT_GUID;
	}

	const std::unordered_map<std::string, TypeExtension>& AssetDatabase::GetAssetTypeExtensionsMap() const
	{
		return m_assetTypeExtensionsMap;
	}

	std::span<const AssetProperty> AssetDatabase::GetTypeMetaVec(const std::string& a_type) const
	{
		auto _it = m_typeMetaMap.find(a_type);
		if (_it != m_typeMetaMap.end())
		{
			return _it->second;
		}
		return std::span<const AssetProperty>();
	}

	const AssetProperty* AssetDatabase::GetAssetProperty(const Core::GUID& a_guid) const
	{
		auto _it = m_assetMap.find(a_guid);
		if (_it != m_assetMap.end())
		{
			return &_it->second;
		}

		// 空の GUID は「未設定」なので探し損ねではない(エディターが毎フレーム引くと流れ続ける)
		if (a_guid.IsValid())
		{
			ENGINE_LOG("アセットが見つかりませんでした : %s",a_guid.String().c_str());
		}
		return nullptr;
	}

	const AssetProperty* AssetDatabase::GetAssetProperty(const std::string& a_filePath) const
	{
		return GetAssetProperty(GetGUIDFromFilePath(a_filePath));
	}

	AssetProperty* AssetDatabase::FindAssetProperty(const Core::GUID& a_guid)
	{
		std::unique_lock _gard(m_mutex);
		auto _it = m_assetMap.find(a_guid);
		if (_it == m_assetMap.end()) return nullptr;

		return &_it->second;
	}

	

	void AssetDatabase::FileWatch()
	{
		while (m_isWatching)
		{

			// ディレクトリ以下の更新を調べる
			DWORD _bytesReturned = 0;

			if (!ReadDirectoryChangesW(
				m_dirHandle,							// 監視するディレクトリのハンドル
				m_buffer.data(),						// 通知を書き込むバッファ
				static_cast<DWORD>(m_buffer.size()),	// バッファサイズ
				TRUE,									// サブディレクトリも監視
				FILE_NOTIFY_CHANGE_FILE_NAME |			// 作成・削除・名前変更
				FILE_NOTIFY_CHANGE_LAST_WRITE |			// ファイル変更
				FILE_NOTIFY_CHANGE_SIZE,				// サイズ変更
				&_bytesReturned,						// 実際に書き込まれたサイズ
				nullptr,								// 同期処理なのでnullptr
				nullptr									// 非同期完了ルーチンを行わない
			))
			{
				if (!m_isWatching) break;
				ENGINE_ERRLOG(false, "[AssetDatabase] ディレクトリ変更監視に失敗");
				break;
			}

			{

				std::unique_lock _gard(m_mutex);

				// 変更内容を取得
				auto* _info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(m_buffer.data());

				while (true)
				{
					// ファイルパス : UTF-16 -> std::wstring
					std::filesystem::path _fileName(_info->FileName, _info->FileName + _info->FileNameLength / sizeof(wchar_t));
					std::filesystem::path _filePath = m_assetsFilePath / _fileName;			// Assetを含めたパスになる

					// 変更内容
					switch (_info->Action)
					{
					case FILE_ACTION_ADDED:
					{
						// 追加
						AddFileProperty(_filePath);
						break;
					}
					case FILE_ACTION_REMOVED:
					{
						// 削除
						RemoveFileProperty(_filePath);
						break;
					}
					case FILE_ACTION_MODIFIED:
					{
						// 変更 : GUIDとファイルの位置だけを見ているので、内容が変わっても処理しない
						break;
					}
					case FILE_ACTION_RENAMED_OLD_NAME:
					{
						// 名前変更前
						break;
					}
					case FILE_ACTION_RENAMED_NEW_NAME:
					{
						// 名前変更後
						break;
					}
					}


					// 次がなければ抜ける
					if (_info->NextEntryOffset == 0) break;

					// 次の通知へオフセット分進める
					_info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<std::byte*>(_info) + _info->NextEntryOffset);
				}

				m_isDirtyDir = true;
			}
		}
	}

	nlohmann::json AssetDatabase::CreateMetaData(const std::filesystem::path& a_srcFile)
	{
		nlohmann::json _json;

		// 新しいGUIDを発行
		Core::GUID _guid = {};
		_guid.Create();
		_json["GUID"] = _guid.String();

		// ファイル拡張子からタイプを推測して保存
		std::string _srcExt = a_srcFile.extension().string();
		std::string _typeStr = "Unknown";

		// サポートされているか調べる
		for (auto& [_type, _typeExt] : m_assetTypeExtensionsMap)
		{
			for (auto& _ext : _typeExt.extensions)
			{
				if (_srcExt != _ext) continue;
				
				// 一致したら保存
				_typeStr = _type;
			}
		}

		// JSONに追加
		_json["Type"] = _typeStr;
		
		return _json;
	}
	bool AssetDatabase::IsSupported(const std::filesystem::path& a_filepath)
	{
		// 拡張子のみを抜き出して文字列化
		std::string _fileExt = a_filepath.extension().string();

		// サポートされた拡張子と一致するならTrue
		for (auto& [_type, _typeExt] : m_assetTypeExtensionsMap)
		{
			// ベース拡張子のチェック (.gltf など)
			for (auto& _ext : _typeExt.extensions)
			{
				if (_fileExt == _ext) return true;
			}

			// 独自規格のチェック (.ob, .oj)
			for (auto& _tExt : _typeExt.typeExt)
			{
				if (_fileExt.find(_tExt) == 0) return true;
			}
		}

		// 対象がサポートされていない
		return false;
	}
	void AssetDatabase::RefreshAssetTree()
	{
		// 階層構造リセット
		m_assetRootNode.Clear();

		for (auto& [_type, _prop] : m_assetMap)
		{
			AssetNode* _pCurrent = &m_assetRootNode;
			std::filesystem::path _path(_prop.filePath);

			// ベースパスからの相対パスへの変換
			auto _relPath = std::filesystem::relative(_path, m_assetsFilePath);

			// フォルダ階層を辿る
			for (auto& _part : _relPath.parent_path())
			{
				std::string _folderName = _part.string();
				if (_folderName == "." || _folderName == "..") continue;
				_pCurrent = &_pCurrent->children[_folderName];
			}

			// アセットを追加
			_pCurrent->assets.push_back(&_prop);
		}

		return;
	}
	void AssetDatabase::AddFileProperty(const std::filesystem::path& a_filePath)
	{
		auto _assetType = GetAssetType(a_filePath);			// アセットタイプを取得
		if (_assetType.empty())
		{
			//空なら必要のないアセットなので処理を飛ばす
			return;
		}

		ENGINE_LOG(
			"[Resource] ファイルの新規追加を検出しました。Type : %s, Path : %s", _assetType.c_str(), a_filePath.string().c_str()
		);

		// 入力されたパスから拡張子を取り除き、ベースパスとして正規化する
		std::filesystem::path _basePath = a_filePath.parent_path() / a_filePath.stem();
		std::string _normBasePath = _basePath.lexically_normal().generic_string();
		std::string _fileExt = a_filePath.extension().string();

		//--------------------------------------------------------------------------
		// まだメインスレッドへ渡していない変更を先に見る
		//
		// 1回の通知で同じアセットの拡張子が並んで届く(.gltf と .obmdl をまとめて
		// 置いたときなど)。ここより先に m_assetMap から作り直してしまうと、
		// 直前に積んだ拡張子が毎回上書きで消える
		//--------------------------------------------------------------------------
		for (auto& [_guid, _pendingProp] : m_changedAssetPropMap)
		{
			// 登録されているベースパス、アセットタイプと比較
			if (_pendingProp.filePath != _normBasePath) continue;
			if (_pendingProp.type != _assetType) continue;

			// 同じ拡張子が二重に届くことがあるので重複は積まない
			auto& _extVec = _pendingProp.extensionsVec;
			if (std::find(_extVec.begin(), _extVec.end(), _fileExt) == _extVec.end())
			{
				_extVec.push_back(_fileExt);
			}
			return;
		}

		// 既存のものと同じグループがあればそこに所属する
		for (const auto& [_guid, _assetProp] : m_assetMap)
		{
			// 登録されているベースパス、アセットタイプと比較
			if (_assetProp.filePath != _normBasePath) continue;
			if (_assetProp.type != _assetType) continue;

			// 既存のプロパティを変更して、保存
			AssetProperty _newProp = _assetProp;
			auto& _extVec = _newProp.extensionsVec;
			if (std::find(_extVec.begin(), _extVec.end(), _fileExt) == _extVec.end())
			{
				_extVec.push_back(_fileExt);
			}
			m_changedAssetPropMap[_guid] = _newProp;
			return;
		}

		// 追加待ちのグループに同じベースパスがあればそこへ足す
		// (作りかけのグループを見ないと、同じアセットのグループが拡張子の数だけ出来る)
		auto& _groupVec = m_typeAssetGroupTemp[_assetType];
		for (auto& _group : _groupVec)
		{
			if (_group.basePath != _normBasePath) continue;

			if (std::find(_group.extensions.begin(), _group.extensions.end(), _fileExt)
				== _group.extensions.end())
			{
				_group.extensions.push_back(_fileExt);
			}
			return;
		}

		// なければ新規作成
		AssetGroup _group = {};
		_group.fileName = _basePath.filename().string();
		_group.basePath = _normBasePath;
		_group.type = _assetType;
		_group.extensions.push_back(_fileExt);
		_groupVec.push_back(_group);
	}
	void AssetDatabase::RemoveFileProperty(const std::filesystem::path & a_filePath)
	{
		auto _assetType = GetAssetType(a_filePath);			// アセットタイプを取得
		if (_assetType.empty())
		{
			//空なら必要のないアセットなので処理を飛ばす
			return;
		}

		ENGINE_LOG(
			"[Resource] ファイルの削除を検出しました。Type : %s, Path : %s", _assetType.c_str(), a_filePath.string().c_str()
		);

		// 入力されたパスから拡張子を取り除き、ベースパスとして正規化する
		std::filesystem::path _basePath = a_filePath.parent_path() / a_filePath.stem();
		std::string _normBasePath = _basePath.lexically_normal().generic_string();
		std::string _fileExt = a_filePath.extension().string();

		//--------------------------------------------------------------------------
		// まだメインスレッドへ渡していない変更を先に見る
		//
		// .gltf と .obmdl をまとめて消すと通知が2件並んで届く。
		// ここより先に m_assetMap から作り直すと1件目で消した拡張子が戻ってしまい、
		// 配列がいつまでも空にならない = メタファイルが消えないままになる
		//--------------------------------------------------------------------------
		for (auto& [_guid, _pendingProp] : m_changedAssetPropMap)
		{
			// 登録されているベースパス、アセットタイプと比較
			if (_pendingProp.filePath != _normBasePath) continue;
			if (_pendingProp.type != _assetType) continue;

			std::erase(_pendingProp.extensionsVec, _fileExt);
			return;
		}

		// 既存のものと同じグループがあればそこから取り除く
		for (const auto& [_guid, _assetProp] : m_assetMap)
		{
			// 登録されているベースパス、アセットタイプと比較
			if (_assetProp.filePath != _normBasePath) continue;
			if (_assetProp.type != _assetType) continue;

			// 変更用のプロパティ
			AssetProperty _newProp = _assetProp;

			// 配列上に今回のがあれば取り除く
			std::erase(_newProp.extensionsVec, _fileExt);

			// 変更されたデータのみ入れて削除はメインスレッド側で一括で行う
			m_changedAssetPropMap[_guid] = _newProp;
			return;
		}

		//--------------------------------------------------------------------------
		// 追加待ちのグループから取り除く
		//
		// 置いてすぐ消した場合、まだ m_assetMap には入っていない。
		// ここで抜かないと、消えたファイルのメタファイルを Update が作ってしまう
		//--------------------------------------------------------------------------
		auto _groupIt = m_typeAssetGroupTemp.find(_assetType);
		if (_groupIt == m_typeAssetGroupTemp.end()) return;

		auto& _groupVec = _groupIt->second;
		for (auto& _group : _groupVec)
		{
			if (_group.basePath != _normBasePath) continue;

			std::erase(_group.extensions, _fileExt);
			break;
		}

		// 拡張子が全部無くなったグループは作らない
		std::erase_if(_groupVec, [](const AssetGroup& a_group) { return a_group.extensions.empty(); });
	}
	void AssetDatabase::ChangeFileName(const std::filesystem::path & a_filePath)
	{
		ENGINE_LOG("ファイル名の変更を検出しました : %s", a_filePath.string().c_str());
	}
	std::string AssetDatabase::GetAssetType(const std::filesystem::path& a_filePath) const
	{
		std::string _fileExt = a_filePath.extension().string();

		// サポートされた拡張子と一致するかチェック
		for (auto& [_type, _typeExt] : m_assetTypeExtensionsMap)
		{
			// ベース拡張子のチェック (.gltf など)
			for (const auto& _ext : _typeExt.extensions)
			{
				if (_fileExt == _ext) return _type;
			}

			// 独自規格のチェック (.ob, .oj)
			for (const auto& _tExt : _typeExt.typeExt)
			{
				if (_fileExt.find(_tExt) == 0) return _type;
			}
		}

		// 見つからなければ空を返す
		return {};
	}
}
