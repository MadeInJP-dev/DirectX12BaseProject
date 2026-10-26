#pragma once

namespace Engine
{
	namespace ECS
	{
		struct EngineServices;
	}
}

namespace Engine::Scene
{
	//======================================================================================
	// シーンの設定
	//
	// シーン(BaseScene)ごとに持つ、中身(エンティティ・環境設定)以外の取り決め。
	//
	// ・先読み一覧
	//     シーンが使う予定で、開いたときに前もって読んでおきたいアセットのGUID。
	//     SceneManager がシーンを開くとき、中身を組み立てる前に読み込みを要求する。
	//
	// ・計測フラグ
	//     立てておくと、次にこのシーンを開いてから閉じるまでに読み込まれたアセットを数え、
	//     閉じたところで先読み一覧を置き換える。計測が済んだらフラグは下りる。
	//     計測中は先読みしない : 先読みしたものまで「使った」と数えてしまい、
	//     一度載ったものが一覧から外れなくなるため。
	//
	// ・保存先はシーンファイルと別(<シーン名>.ojscncfg / .obscncfg)
	//     計測が終わるのはシーンを閉じるときで、そのときのシーンの中身はプレイで
	//     動いた後のもの。シーンファイルへ書き戻すと動いた後の状態まで保存してしまうので、
	//     設定だけを書けるよう分けてある。エディターでシーンを保存したときも一緒に書く。
	//======================================================================================
	// シーン遷移のフェード1方向ぶん(暗転の長さ)
	struct SceneFadeSetting
	{
		bool isEnabled = true;		// このフェードを行うか
		float duration = 0.5f;		// かける時間(秒)

		// 実際にフェードを行うか(長さが無ければ行わない)
		bool IsActive() const { return isEnabled && duration > 0.0f; }
	};

	// 重ねたシーンの後ろのぼかし(ポーズ・設定画面・リザルトのように上へ出すシーン用)
	struct SceneBackdropSetting
	{
		bool isEnabled = false;		// このシーンを重ねている間、後ろをぼかすか
		float strength = 1.0f;		// 掛かり具合(0〜1)
		float radius = 24.0f;		// ぼかしの半径(描画解像度のピクセル)
		float duration = 0.35f;		// 掛け始め・外すときにかける時間(秒)

		bool IsActive() const { return isEnabled && strength > 0.0f && radius > 0.0f; }
	};

	class SceneConfig
	{
	public:

		/// <summary>
		/// 保存・読み込み
		/// </summary>
		void Archive(Persistence::Archive& a_ar);

		/// <summary>
		/// シーンファイルと同じ場所にある設定ファイルを読む
		/// </summary>
		/// <param name="a_fileDir">シーンファイルのフォルダ</param>
		/// <param name="a_fileName">シーンファイルの名前(拡張子なし)</param>
		/// <returns>ファイルが無ければ false(既定値のまま)</returns>
		bool LoadFile(const std::string& a_fileDir, const std::string& a_fileName);

		/// <summary>
		/// シーンファイルと同じ場所へ設定ファイルを書く
		/// </summary>
		void SaveFile(const std::string& a_fileDir, const std::string& a_fileName);

		/// <summary>
		/// エディター用の編集欄
		/// </summary>
		/// <param name="a_isRecording">このシーンが計測中か(表示だけに使う)</param>
		void DrawEdit(const ECS::EngineServices& a_services, bool a_isRecording);

		//------------------------------------------------------------------------------------------
		// 計測フラグ
		//------------------------------------------------------------------------------------------
		bool IsRecordPreLoadAssets() const { return m_isRecordPreLoadAssets; }
		void SetRecordPreLoadAssets(bool a_isRecord) { m_isRecordPreLoadAssets = a_isRecord; }

		//------------------------------------------------------------------------------------------
		// 先読み一覧
		//------------------------------------------------------------------------------------------
		const std::vector<Core::GUID>& GetPreLoadAssetGUIDs() const { return m_preLoadAssetGUIDVec; }
		void SetPreLoadAssetGUIDs(std::vector<Core::GUID> a_guidVec) { m_preLoadAssetGUIDVec = std::move(a_guidVec); }

		//------------------------------------------------------------------------------------------
		// シーン遷移のフェード
		//
		// フェードアウト : このシーンから別のシーンへ切り替える(Replace)とき、切り替える前に画面を染める
		// フェードイン   : このシーンが開いて読み込みが済んだとき、染めた画面から戻す
		// 既定は両方あり・黒。設定ファイルが無い / 項目が無い古いシーンもこの既定になる
		//------------------------------------------------------------------------------------------
		const SceneFadeSetting& GetFadeOut() const { return m_fadeOut; }
		const SceneFadeSetting& GetFadeIn() const { return m_fadeIn; }
		const Math::Color& GetFadeColor() const { return m_fadeColor; }

		//------------------------------------------------------------------------------------------
		// 後ろのぼかし
		//
		// このシーンが一番上に重なっている間(下に別のシーンがあるときだけ)、
		// 後ろの 3D をぼかす。UI はぼけない。既定は掛けない
		//------------------------------------------------------------------------------------------
		const SceneBackdropSetting& GetBackdrop() const { return m_backdrop; }

	private:

		void ArchiveFade(Persistence::Archive& a_ar);		// フェードの区切り
		void ArchiveBackdrop(Persistence::Archive& a_ar);	// 後ろのぼかしの区切り

		// 次に開いたときに先読み一覧を計測し直すか
		bool m_isRecordPreLoadAssets = false;

		// 開くときに前もって読んでおくアセット : 計測で初めて要求された順
		std::vector<Core::GUID> m_preLoadAssetGUIDVec = {};

		// シーン遷移のフェード
		SceneFadeSetting m_fadeOut = {};
		SceneFadeSetting m_fadeIn = {};
		Math::Color m_fadeColor = Math::Color(0.0f, 0.0f, 0.0f, 1.0f);	// 染める色(a は使わない)

		// 重ねている間の後ろのぼかし
		SceneBackdropSetting m_backdrop = {};
	};
}
