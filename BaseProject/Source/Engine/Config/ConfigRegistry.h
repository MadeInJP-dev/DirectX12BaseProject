#pragma once

namespace Engine::ECS
{
	struct EngineServices;
}

namespace Engine::Config
{
	// 設定画面での並び(分類ごとにまとめて出す)
	enum class EConfigCategory
	{
		Project,	// 起動設定
		Window,		// ウィンドウ・フレームレート
		Graphics,	// 描画
		Input,		// 入力
		Game,		// ゲーム側(App)の設定
		Debug,		// デバッグ表示
		Count
	};

	// 分類の表示名
	const char* GetConfigCategoryName(EConfigCategory a_category);

	//==========================================================================================
	// 設定1つぶん : 保存ファイルのグループ1つ = 設定画面の欄1つ
	//
	// 値の実体は持ち主(NativeWindow / InputManager など)のメンバ。ここは読み書きと表示の入口だけ
	//==========================================================================================
	struct ConfigSection
	{
		std::string name = "";					// 保存のグループ名
		std::string owner = "";					// 持ち主のクラス名(設定画面に出す)
		EConfigCategory category = EConfigCategory::Project;

		std::function<void(Persistence::Archive&)> archiveFunc = nullptr;
		std::function<void(const ECS::EngineServices&)> drawEditFunc = nullptr;
	};

	//==========================================================================================
	// エンジン設定(EngineData.ojoptn)の読み書きと、設定画面への一覧
	//
	// 設定の値は使うシステムが持ち、ここは持たない。持ち主が自分の設定を Register すると、
	// その場でファイルの値が流し込まれる(持ち主を作った直後に呼べば、使う前に値が揃う)。
	// 保存は登録中の設定をすべて書き出す。登録を外した設定(先に壊れた App 側など)は、
	// 外したときの値を覚えておいて一緒に書く。
	//
	// 形式は JSON 固定。JSON を読むかどうか(起動設定の isLoadJsonData)を決めているのが
	// このファイル自身で、読む時点ではまだ起動設定が分からないため
	//==========================================================================================
	class ConfigRegistry
	{
	public:

		ConfigRegistry();
		~ConfigRegistry();
		NON_COPYABLE_NON_MOVABLE(ConfigRegistry);

		// ファイルを読んでおく : 誰よりも先に1回。以降の Register はここで読んだ値を流し込む
		void Load(const std::string& a_filePath);

		/// 持ち主の設定を登録し、読んである値を流し込む
		///
		/// @param a_name   保存のグループ名
		/// @param a_owner  持ち主のクラス名(設定画面に出すだけ)
		/// @param a_config 設定の実体。Archive(Archive&) と DrawEdit(const EngineServices&) を持つこと。
		///                 登録を外す(Unregister)まで生きていること
		template<class T>
		void Register(const char* a_name, const char* a_owner, EConfigCategory a_category, T& a_config)
		{
			ConfigSection _section = {};
			_section.name = a_name;
			_section.owner = a_owner;
			_section.category = a_category;
			_section.archiveFunc = [pConfig = &a_config](Persistence::Archive& a_ar) { pConfig->Archive(a_ar); };
			_section.drawEditFunc = [pConfig = &a_config](const ECS::EngineServices& a_services) { pConfig->DrawEdit(a_services); };
			RegisterSection(std::move(_section));
		}

		// 登録を外す。今の値は覚えておき、次の保存で書く
		void Unregister(const std::string& a_name);

		// 保存 : 登録中の値と、外した設定の覚えている値を書き出す
		void Save();

		// ファイルの値で登録中の設定を戻す(設定画面の「元に戻す」)
		void Reload();

		// 保存してから値が変わったか(設定画面の「未保存」の印)
		bool IsDirty() const;

		const std::vector<ConfigSection>& GetSections() const { return m_sectionVec; }

	private:

		void RegisterSection(ConfigSection&& a_section);

		// 覚えている値 → 設定
		void LoadSection(const ConfigSection& a_section) const;

		// 設定 → a_json のグループ
		static void StoreSection(const ConfigSection& a_section, nlohmann::json& a_json);

	private:

		std::string m_filePath = "";

		// ファイルの中身(最後に読んだ・書いた値)。外した設定の値もここに残る
		std::unique_ptr<nlohmann::json> m_upSavedJson = nullptr;

		std::vector<ConfigSection> m_sectionVec = {};
	};
}
