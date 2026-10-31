#pragma once

#include "Engine/Graphics/Particle/Core/ParticleData.h"

namespace Engine::Resource
{
	//==========================================================================================
	// パーツの上限 : コンポーネント側で使われるため、固定長の要素数
	//==========================================================================================
	// パーティクルは 8。爆発は閃光・火・煙・火花・破片・火の粉…と層が多く、4 では足りない
	// (1つ増やすと、出ているエフェクト1個あたり 9 バイト増える)
	inline constexpr size_t EFFECT_PARTICLE_MAX = 8;
	inline constexpr size_t EFFECT_MESH_MAX = 4;
	inline constexpr size_t EFFECT_SOUND_MAX = 4;
	inline constexpr size_t EFFECT_POINTLIGHT_MAX = 1;
	inline constexpr size_t EFFECT_TRAIL_MAX = 2;

	// トレイル1本が持てる点の上限(EffectTrailPart::maxPoints の上限)
	inline constexpr int EFFECT_TRAIL_POINT_MAX = 128;

	// 個体ごとのパラメータの数と、その結び付けの上限
	inline constexpr size_t EFFECT_PARAM_MAX = 4;
	inline constexpr size_t EFFECT_PARAM_BINDING_MAX = 8;

	//==========================================================================================
	// 発生源の取り方 : エフェクトがついているものにたいして基準を選ぶ 末尾に追加必須
	//
	// 「出す瞬間の位置と向きをどう決めるか」だけを選ぶ。
	// 出したあとの粒をどの座標系で回すか(シミュレーション空間)は EEffectSimulationSpace。
	// 名前に Local / World が入っているが、両者は別物なので混ぜないこと
	//==========================================================================================
	enum class EEffectSpace : uint32_t
	{
		LocalOffset,		// 相手の行列基準でオフセット/方向を合成する(既定。ノズル位置など)
		WorldMatrix,		// 相手の位置と前方(+Z)をそのまま使う
		ReverseVelocity,	// 位置は行列基準、向きは進行方向の逆(噴射・排気)。
							// 見た目の姿勢と進行方向が一致しない弾やミサイル向け
	};

	const char* ToString(EEffectSpace a_space);

	//==========================================================================================
	// 出した粒をどの座標系で回すか(パーツ単位の上書き) : 末尾に追加必須(値は保存される)
	//
	//   Inherit : パーティクルアセットの SimulationSpace に従う(既定)
	//   World   : 出した瞬間にワールドへ置き、そのまま回す。発生源が動いても置き去り(煙・爆発・軌跡)
	//   Local   : 発生源(エフェクトの持ち主)の座標系で回し、描くときにワールドへ戻す。
	//             発生源にくっついて動く(ブースターの噴射)
	//
	// どちらでも、出す瞬間の位置・向き・散らばりは同じ式で決まる(違うのは出したあとに追従するかだけ)。
	// 粒は自分がどの発生源の座標系かを席番号で持つので、同じパーティクルアセットを
	// あるエフェクトでは Local、別のエフェクトでは World で使ってよい
	//==========================================================================================
	enum class EEffectSimulationSpace : uint32_t
	{
		Inherit,	// パーティクルアセットの設定に従う
		World,		// ワールドで回す
		Local,		// 発生源の座標系で回す
	};

	const char* ToString(EEffectSimulationSpace a_space);

	//==========================================================================================
	// パーツがいつ動き出すか : 末尾に追加必須(値は保存される)
	//==========================================================================================
	enum class EEffectTrigger : uint32_t
	{
		OnPlay,		// 再生を始めてから(既定)。duration 0 なら止めるまで出し続ける(= 継続中)
		OnStop,		// 止めてから。消火の火花・終了音など。
					// duration 0 なら一度きり(バースト・単発音)。止めたあと、これが全部出し終わるまでを「止めている最中」とする
	};

	const char* ToString(EEffectTrigger a_trigger);

	//==========================================================================================
	// 個体ごとのパラメータが効く先 : 末尾に追加必須(値は保存される)
	//
	// どれも倍率として掛かる(1 で変化なし)
	//==========================================================================================
	enum class EEffectParamTarget : uint32_t
	{
		ParticleEmitCount,	// パーティクルの発生数
		ParticleSpeed,		// パーティクルの初速(束の長さ)
		ParticleSize,		// パーティクルの粒の大きさ
		MeshScale,			// メッシュの大きさ
		MeshEmissive,		// メッシュの発光の強さ
		SoundVolume,		// 音量
	};

	const char* ToString(EEffectParamTarget a_target);

	//==========================================================================================
	// 個体ごとのパラメータの結び付け
	//
	// 出す側(EffectOverrideComponent::params)が書いた値(0〜1 を想定)を、
	// アセットのどこに、どれだけ効かせるかを決める。
	//   倍率 = lerp(scaleAtZero, scaleAtOne, params[paramIndex])
	// 例) ブーストの溜まり具合を params[0] に入れ、ParticleSize を 1 → 1.6 倍にする
	//
	// パラメータを書かない(0 のまま)なら scaleAtZero が掛かる。既定は 1 → 1 で何も変わらない
	//==========================================================================================
	struct EffectParamBinding
	{
		int paramIndex = 0;									// どのパラメータか(0 〜 EFFECT_PARAM_MAX-1)
		EEffectParamTarget target = EEffectParamTarget::ParticleEmitCount;
		int partIndex = -1;									// どのパーツに効かせるか(-1 で、その種類のパーツ全部)
		float scaleAtZero = 1.0f;							// パラメータが 0 のときの倍率
		float scaleAtOne = 1.0f;							// パラメータが 1 のときの倍率

		// 値から倍率を出す(0〜1 の外も、そのまま延長する)
		float Evaluate(float a_value) const { return scaleAtZero + (scaleAtOne - scaleAtZero) * a_value; }

		void Archive(Persistence::Archive& a_ar);
	};

	//==========================================================================================
	// パーツ共通の時間指定
	//
	// 時間を数え始めるのは、trigger が OnPlay なら再生を始めたとき、OnStop なら止めたとき。
	// 下の関数に渡す経過時間も、それぞれの起点からの秒数
	//==========================================================================================
	struct EffectTiming
	{
		float startDelay = 0.0f;	// 起点からこの秒数だけ待ってから出る
		float duration = 0.0f;		// 出している長さ(秒)。0 なら止めるまで出しっぱなし(OnStop なら一度きり)

		// いつ動き出すか。
		// ※ 保存は各パーツの Archive の末尾で行う(EffectTiming::Archive はパーツの並びの途中で呼ばれるので、
		//    ここに足すとバイナリの読み位置がずれる)
		EEffectTrigger trigger = EEffectTrigger::OnPlay;

		bool IsStopTrigger() const { return trigger == EEffectTrigger::OnStop; }

		// 経過時間から見て、今出している最中か
		bool IsActiveAt(float a_elapsed) const
		{
			if (a_elapsed < startDelay) return false;
			if (duration <= 0.0f) return true;	// 出しっぱなし
			return a_elapsed < (startDelay + duration);
		}

		// 出し終わっているか(0〜1のうち、終わりまで来たか)
		bool IsFinishedAt(float a_elapsed) const
		{
			if (duration <= 0.0f) return false;	// 出しっぱなしは終わらない
			return a_elapsed >= (startDelay + duration);
		}

		// 出している区間のどこまで進んだか(0〜1)。出しっぱなしのときは常に 0
		float GetProgressAt(float a_elapsed) const
		{
			if (duration <= 0.0f) return 0.0f;
			const float _t = (a_elapsed - startDelay) / duration;
			return std::clamp(_t, 0.0f, 1.0f);
		}

		// 止めている最中のパーツ(OnStop)が、もう出し終わったか。
		// duration 0 は一度きりなので、出し始めの時間が来れば終わり扱い
		// (出す処理はその時間が来たフレームに走るので、取りこぼさない)
		bool IsStopPartDoneAt(float a_stopElapsed) const
		{
			if (duration <= 0.0f) return a_stopElapsed >= startDelay;
			return a_stopElapsed >= (startDelay + duration);
		}

		void Archive(Persistence::Archive& a_ar);
	};

	//==========================================================================================
	// パーティクル
	//==========================================================================================
	struct EffectParticlePart
	{
		// ---- 何を出すか ----
		Core::GUID particleGUID = Core::DEFAULT_GUID;
		Handle<ParticlesAsset> particleHandle = {};		// ランタイム用(読み込み時に解決)

		// ---- どこから出すか ----
		// 位置と向きはエフェクトの置き場から見た値(持ち主のスケールは掛からない。
		// 粒の大きさ・速さ・散らばりにも掛からないのと揃えてある)
		EEffectSpace space = EEffectSpace::LocalOffset;
		Math::Vector3 posOffset = { 0.0f, 0.0f, 0.0f };	// 相手の行列基準の発生位置
		Math::Vector3 emitDir = { 0.0f, 0.0f, 1.0f };	// 相手の行列基準の発生方向

		// ---- どっちへ出すか ----
		// Cone       : emitDir を軸にした円錐(directionAngle が広がり)。噴射・排気
		// Sphere     : 中心から全方向へ均等。爆発
		// Hemisphere : emitDir 側の半球だけ。地面での爆発
		// ※ Cone の角度を 360 にしても全方向にはならない(円錐の半頂角なので)。
		//    四方八方へ飛び散らせたいときは Sphere を選ぶこと
		Graphics::Particle::EParticleEmitShape emitShape = Graphics::Particle::EParticleEmitShape::Cone;

		// ---- どれだけ出すか ----
		int   emitCount = 8;		// 1回の発生数
		float emitRate = 0.0f;		// >0 : 毎秒この回数だけ発生 / 0 : 開始時に1回だけ(バースト)

		// ---- いつ出すか ----
		EffectTiming timing = {};

		// ---- 散らばり方 ----
		// 1粒の速度・寿命はアセット側。ここは「どれくらいの範囲にどう散らすか」
		float baseScale = 1.0f;			// 全体スケール
		float minScale = 0.1f;			// 1粒のスケール下限
		float maxScale = 1.0f;			// 1粒のスケール上限
		float positionRadius = 0.5f;	// 発生位置のばらつき半径
		float directionAngle = 10.0f;	// 発生方向のばらつき(度)。Cone のときだけ効く

		// 発生位置を箱の中へ散らす半分の大きさ(m)。0 なら箱は使わない(半径だけ)。
		// 軸は発生の向き基準 : X = 横、Y = 上、Z = 噴き出す向き(EmitDir)。
		// 地面を這う霧のように「広くて薄い」範囲へ出したいときに使う
		// (半径は球なので、横に広げると同じだけ上下にも広がってしまう)。
		// 半径と両方あるときは、箱の中の点からさらに半径ぶん散らす
		Math::Vector3 positionExtents = { 0.0f, 0.0f, 0.0f };

		// ---- 出したあと、どの座標系で回すか ----
		EEffectSimulationSpace simulationSpace = EEffectSimulationSpace::Inherit;

		bool IsValid() const { return particleGUID != Core::DEFAULT_GUID; }

		/// <summary>
		/// 発生源の座標系で回すか(パーツの上書き → パーティクルアセットの設定の順で決める)
		/// </summary>
		/// <param name="a_pParticle">このパーツのパーティクルアセット。引けていなければ nullptr(World 扱い)</param>
		bool IsLocalSimulation(const ParticlesAsset* a_pParticle) const;

		void Archive(Persistence::Archive& a_ar);
	};

	//==========================================================================================
	// メッシュ : 芯の表現などはっきりしたもの
	//==========================================================================================
	struct EffectMeshPart
	{
		// ---- 何を出すか ----
		Core::GUID modelGUID = Core::DEFAULT_GUID;
		Handle<Model> modelHandle = {};					// ランタイム用(読み込み時に解決)

		// ---- どこに出すか : 相手の行列基準のローカル配置 ----
		Math::Vector3 posOffset = { 0.0f, 0.0f, 0.0f };
		Math::Vector3 rotation = { 0.0f, 0.0f, 0.0f };	// オイラー角(度)
		Math::Vector3 scale = { 1.0f, 1.0f, 1.0f };

		// ---- いつ出すか ----
		EffectTiming timing = {};

		// ---- 見た目 ----
		Math::Color colorScale = { 1.0f, 1.0f, 1.0f, 1.0f };
		Math::Vector3 emissiveColor = { 1.0f, 1.0f, 1.0f };	// 発光色(0〜1)
		float emissiveIntensity = 0.0f;						// 発光の強さ(0でオフ)。
															// ブルームのしきい値を超えると光って見える

		// ---- 時間で変える終値 ----
		Math::Vector3 endScale = { 1.0f, 1.0f, 1.0f };	// duration の終わりでのスケール倍率
		float endAlpha = 1.0f;							// duration の終わりでの不透明度
		float endEmissiveIntensity = 0.0f;				// duration の終わりでの発光の強さ

		bool IsValid() const { return modelGUID != Core::DEFAULT_GUID; }

		void Archive(Persistence::Archive& a_ar);
	};

	//==========================================================================================
	// サウンド : 座標はエフェクトの中心
	//==========================================================================================
	struct EffectSoundPart
	{
		// ---- 何を鳴らすか ----
		Core::GUID soundGUID = Core::DEFAULT_GUID;
		Handle<Sound> soundHandle = {};	// ランタイム用(読み込み時に解決)。
										// 鳴らす瞬間に波形の読み込みが走らないよう先に持っておく

		// ---- いつ鳴らすか ----
		// startDelay : 再生開始から何秒後に鳴らし始めるか
		// duration   : ループ音を鳴らし続ける長さ(0 なら止めるまで)。
		//              単発音(isLoop = false)では使わない
		EffectTiming timing = {};

		// ---- どう鳴らすか ----
		float vol = 1.0f;			// 音量。1 で 100%
		bool  isLoop = false;		// 鳴りっぱなしにするか(噴射音など)。エフェクトを止めれば止まる
		bool  is3DSound = true;		// 位置による定位・距離減衰を付けるか。false なら常に同じ音量で鳴る
		float distanceScaler = 1.0f;// 3D の減衰倍率。大きいほど遠くまで届く(3D のときだけ効く)

		// ---- 出し切ったかの判定に混ぜるか ----
		// true : この音が鳴り終わるまで「エフェクトは終わっていない」とみなす。
		//        destroyOnFinish のエフェクト(単発の爆発など)で、絵が消えた瞬間に
		//        エンティティごと消えて音が途切れるのを防ぐ。
		// false: 音の長さを見ない。絵が終わればエフェクトも終わる
		bool isWaitFinish = true;

		//------------------------------------------------------------------
		// 鳴らしすぎない(同じ音ごと。AudioManager の関所で数える)
		//
		// 一発もの(被弾・爆発など)は鳴らすたびに別のエンティティになるので、
		// エンティティ側では間引けない。同じ音を、エフェクトをまたいで間引く。
		// 間引かれた音は鳴らさずに「鳴らした」扱いにする(後から遅れて鳴らない)
		//------------------------------------------------------------------
		float minInterval = 0.0f;	// 前回鳴らしてからこの秒数が経つまでは鳴らさない(0 で制限なし)
		int   maxConcurrent = 0;	// 同時に鳴っている数の上限(0 で制限なし)

		bool IsValid() const { return soundGUID != Core::DEFAULT_GUID; }

		void Archive(Persistence::Archive& a_ar);
	};

	//==========================================================================================
	// ライト : 爆発の閃光・噴射の照り返しなど、まわりを照らすもの
	//
	// 出している間だけ LightManager からポイントライトを借りる(EffectDrawSystem)。
	// 位置はエフェクトの置き場(上書きの位置・向き・大きさ込み)を基準にする
	//==========================================================================================
	struct EffectLightPart
	{
		// ---- どこに出すか : 置き場の行列基準のローカル位置 ----
		Math::Vector3 posOffset = { 0.0f, 0.0f, 0.0f };

		// ---- 見た目 ----
		Math::Color color = { 1.0f, 1.0f, 1.0f, 1.0f };	// 色
		float brightness = 1.0f;							// 色に掛ける強さ
		float range = 5.0f;									// 光の届く距離(m)。エフェクト全体の大きさ倍率も掛かる

		// ---- いつ出すか ----
		EffectTiming timing = {};

		// ---- 時間で変える終値 ----
		float endBrightness = 1.0f;		// duration の終わりでの強さ(閃光なら 0 へ落とす)

		bool IsValid() const { return range > 0.0f; }

		void Archive(Persistence::Archive& a_ar);
	};

	//==========================================================================================
	// トレイルの色の重ね方 : 末尾に追加必須(値は保存される)
	//==========================================================================================
	enum class EEffectTrailBlend : uint32_t
	{
		AlphaBlend,	// 下地を α のぶん隠して色を置く(煙の筋・布)
		Additive,	// 下地へ足す(光の軌跡・剣筋)
	};

	const char* ToString(EEffectTrailBlend a_blend);

	//==========================================================================================
	// トレイル : 動いた跡に残る帯(剣筋・ミサイルの光跡など)
	//
	// 出している間、置き場から見た posOffset の位置を毎フレーム点として残し、
	// 点を順につないだ帯をカメラへ向けて描く(TransparentPass)。
	// 点の履歴は実行中の値なので、ここではなく Effect::TrailNode が持つ。
	// 止めても(出す時間帯を外れても)点は消えず、lifeTime をかけて尾から消えていく
	//==========================================================================================
	struct EffectTrailPart
	{
		// ---- どこから引くか : 置き場の行列基準のローカル位置 ----
		Math::Vector3 posOffset = { 0.0f, 0.0f, 0.0f };

		// ---- いつ引くか ----
		EffectTiming timing = {};

		// ---- 帯の長さ ----
		float lifeTime = 0.3f;				// 1つの点が残る秒数。長いほど帯が長く残る
		int   maxPoints = 32;				// 点の数の上限(2〜EFFECT_TRAIL_POINT_MAX)。超えたら古い点から捨てる
		float minVertexDistance = 0.1f;		// 前の点からこの距離(m)動くまでは新しい点を足さない(先頭だけ動かす)

		// ---- 見た目 ----
		float width = 0.5f;											// 先頭の幅(m)。エフェクト全体の大きさ倍率も掛かる
		float endWidth = 0.0f;										// 尾の幅(m)
		Math::Color headColor = { 1.0f, 1.0f, 1.0f, 1.0f };		// 先頭の色(a は濃さ)
		Math::Color tailColor = { 1.0f, 1.0f, 1.0f, 0.0f };		// 尾の色
		float intensity = 1.0f;										// 色に掛ける強さ。1 を超えるとブルームで光る
		EEffectTrailBlend blend = EEffectTrailBlend::AlphaBlend;

		// 帯に貼る絵(U = 先頭 → 尾、V = 帯の端 → 端)。空なら色だけ
		Core::GUID textureGUID = Core::DEFAULT_GUID;
		Handle<Texture> textureHandle = {};		// ランタイム用(読み込み時に解決)

		bool IsValid() const { return lifeTime > 0.0f && maxPoints >= 2; }

		void Archive(Persistence::Archive& a_ar);
	};

	//==========================================================================================
	// エフェクト
	//
	// メッシュ形状やパーティクル、鳴らす音をまとめて扱う。
	// 例) ジェット噴射されている中心は白い縦長のメッシュで、周りにパーティクルを散らす。
	//     爆発なら、粒と火球に「ドン」という音まで込みで1枚。
	//
	// 使う側は「再生する・止める」だけを伝えればよく、
	// 何個のパーティクルとメッシュと音で出来ているかを知らなくてよい。
	// (音だけ別に鳴らしに行かなくてよい、というのがサウンドパーツの狙い)
	//
	// ここが持つのは設計図(パーツの定義)だけ。
	// 再生(時間を進める・音を鳴らす・メッシュの置き方を組む)は Engine::Effect::EffectPlayer、
	// 実行中の値は Engine::Effect::EffectInstance が持つ
	//==========================================================================================
	class EffectAsset
	{
	public:
		EffectAsset() = default;
		explicit EffectAsset(const std::string& a_name) : m_name(a_name) {}
		~EffectAsset() = default;
		NON_COPYABLE_MOVABLE(EffectAsset);

		//--------------------------------------------------------------------
		// 定義へのアクセス
		//--------------------------------------------------------------------
		const std::vector<EffectParticlePart>& GetParticleParts() const { return m_particleParts; }
		const std::vector<EffectMeshPart>& GetMeshParts() const { return m_meshParts; }
		const std::vector<EffectSoundPart>& GetSoundParts() const { return m_soundParts; }

		// 編集用 : エディターから直接書き換える
		std::vector<EffectParticlePart>& RefParticleParts() { return m_particleParts; }
		std::vector<EffectMeshPart>& RefMeshParts() { return m_meshParts; }
		std::vector<EffectSoundPart>& RefSoundParts() { return m_soundParts; }

		// ライト
		const std::vector<EffectLightPart>& GetLightParts() const { return m_lightParts; }
		std::vector<EffectLightPart>& RefLightParts() { return m_lightParts; }
		bool AddLightPart();
		void RemoveLightPart(size_t a_index);

		// トレイル
		const std::vector<EffectTrailPart>& GetTrailParts() const { return m_trailParts; }
		std::vector<EffectTrailPart>& RefTrailParts() { return m_trailParts; }
		bool AddTrailPart();
		void RemoveTrailPart(size_t a_index);

		// 個体ごとのパラメータの結び付け
		const std::vector<EffectParamBinding>& GetParamBindings() const { return m_paramBindings; }
		std::vector<EffectParamBinding>& RefParamBindings() { return m_paramBindings; }
		bool AddParamBinding();
		void RemoveParamBinding(size_t a_index);

		/// <summary>
		/// 個体ごとのパラメータから、効く先の倍率を出す(結び付けが無ければ 1)
		/// </summary>
		/// <param name="a_partIndex">そのパーツの番号(結び付けの partIndex が -1 なら全部に効く)</param>
		/// <param name="a_pParams">EFFECT_PARAM_MAX 個の値。null なら全部 0 として扱う</param>
		float EvaluateParamScale(EEffectParamTarget a_target, size_t a_partIndex, const float* a_pParams) const;

		// その効き先への結び付けを1つでも持っているか
		bool HasParamBinding(EEffectParamTarget a_target) const;

		// 追加・削除 : 上限を超えないようにここを通す
		bool AddParticlePart();
		bool AddMeshPart();
		bool AddSoundPart();
		void RemoveParticlePart(size_t a_index);
		void RemoveMeshPart(size_t a_index);
		void RemoveSoundPart(size_t a_index);

		const std::string& GetName() const { return m_name; }
		void SetName(const std::string& a_name) { m_name = a_name; }

		//--------------------------------------------------------------------
		// 保存・読み込み
		//--------------------------------------------------------------------
		void Archive(Persistence::Archive& a_ar);
		void Save(const std::string& a_baseFilePath);

		/// <summary>
		/// GUIDから参照アセット(パーティクル・モデル・サウンド)のハンドルを解決する
		/// </summary>
		/// <remarks>
		/// ロード直後と、エディターで差し替えた後に呼ぶ。
		/// ロードはリソースマネージャー経由なので、アセット単体では解決できない
		/// </remarks>
		void ResolveReferences(ResourceManager& a_resourceManager);

		// 止めたあとに動くパーツ(OnStop)を1つでも持っているか。
		// 持っていれば、止めたあと「止めている最中」を経てから止まりきる
		bool HasStopParts() const;

	private:

		// 識別子
		std::string m_name;

		// パーツ
		std::vector<EffectParticlePart> m_particleParts;
		std::vector<EffectMeshPart> m_meshParts;
		std::vector<EffectSoundPart> m_soundParts;

		// 個体ごとのパラメータの結び付け
		std::vector<EffectParamBinding> m_paramBindings;

		// ライト(上限は EFFECT_POINTLIGHT_MAX)
		std::vector<EffectLightPart> m_lightParts;

		// トレイル(上限は EFFECT_TRAIL_MAX)
		std::vector<EffectTrailPart> m_trailParts;
	};
}
