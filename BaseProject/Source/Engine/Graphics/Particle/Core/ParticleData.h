#pragma once
namespace Engine::Graphics::Particle
{
	//======================================================================================
	// 発生源の席(EmitterSlotPool)を伸ばす単位
	//
	// 席は全アセット共通の1つの表で、足りなくなったらこの数ずつ伸ばす。
	// GPU側のバッファはそのとき作り直す(めったに起きない)。1席 64 バイト
	//======================================================================================
	inline constexpr uint32_t EMITTER_SLOT_BLOCK_SIZE = 64;

	//======================================================================================
	// 席を返すまでの猶予(秒)の代わりの値
	//
	// 持ち主が消えても、出した粒は寿命まで残ってその席の行列を読み続ける。
	// 猶予はふつう「使っているパーティクルアセットの最大寿命」で、粒の寿命はその範囲に収まる。
	// アセットが引けず寿命が分からないときだけこの値を使う
	// (すぐ返して粒が飛ぶより、長めに握る方が害が小さい)
	//======================================================================================
	inline constexpr float EMITTER_SLOT_FALLBACK_HOLD_SECONDS = 5.0f;

	//======================================================================================
	// プールを眠らせるまでの余裕(秒)
	//
	// 最後に出してから「最大寿命 + これ」が経ったプールは、更新も描画も飛ばす。
	// 余裕を取るのは、最後の粒が寿命切れでデッドリストへ返るまで更新を回すため
	// (返す前に止めると、次に起きたときまで空きが減ったままになる)。
	// フレーム時間のぶれと、更新が1フレーム遅れて回るぶんを見込んで少し長めに取る
	//======================================================================================
	inline constexpr double PARTICLE_POOL_SLEEP_MARGIN_SECONDS = 0.5;

	//======================================================================================
	// プールの容量(足りなくなったら伸ばす)
	//
	// 容量はアセットで決め打ちにせず、要る数に合わせて伸ばす。
	// 要る数は CPU 側で数える :「生きている粒の数 ≦ 直近の最大寿命の間に出した粒の合計」
	// (どの粒も最大寿命のうちに消えるので必ず成り立つ。GPU から読み戻さないので遅れが無い)。
	// 伸ばすときは1本のバッファを大きく作り直して、中身を GPU 上で写す(EmitterSlotPool と同じ)。
	//
	//   BLOCK_SIZE     : 伸ばす単位(粒)。1ブロック = 粒 64KB + デッドリスト 4KB + 生存リスト 4KB
	//   GROW_HEADROOM  : 伸ばすときに要る数へ足す余裕(割合)。少しずつ増える演出で毎フレーム伸ばさないため
	//   HARD_LIMIT     : 1プールの上限(暴走対策)。粒 64MB + リスト 8MB。超えたぶんはそのフレームでは出ない
	//   SHRINK_IDLE    : 眠ってから(最後の粒が消えてから)これだけ経ったら、初期の容量へ縮める(秒)
	//======================================================================================
	inline constexpr uint32_t PARTICLE_POOL_BLOCK_SIZE = 1024;
	inline constexpr double PARTICLE_POOL_GROW_HEADROOM = 0.25;
	inline constexpr uint32_t PARTICLE_POOL_HARD_LIMIT = 1u << 20;
	inline constexpr double PARTICLE_POOL_SHRINK_IDLE_SECONDS = 10.0;

	/// ブロック単位に切り上げる(0 なら 1 ブロック。上限で頭打ち)
	inline uint32_t RoundUpToPoolBlock(uint64_t a_count)
	{
		const uint64_t _blocks = (std::max<uint64_t>(a_count, 1) + PARTICLE_POOL_BLOCK_SIZE - 1) / PARTICLE_POOL_BLOCK_SIZE;
		return static_cast<uint32_t>((std::min<uint64_t>)(_blocks * PARTICLE_POOL_BLOCK_SIZE, PARTICLE_POOL_HARD_LIMIT));
	}

	//======================================================================================
	// 半透明の粒の並べ替え(SortParticleShader.hlsl のバイトニックソート)
	//
	// 半透明のアセットの粒は、全アセットぶんを1本の一覧へ集めて遠い順に並べ、1回で描く
	// (アセットごとに描くと、違うアセットの粒どうしの前後が決められない)。
	//
	//   BLOCK_SIZE        : 1グループが共有メモリで並べる要素数。並べ替え用のバッファはこれ以上の2のべき乗
	//   THREAD_GROUP_SIZE : 1グループのスレッド数(1スレッドが比較する1組 = 2要素を受け持つ)
	// ※ シェーダーの SORT_BLOCK_SIZE / SORT_THREAD_GROUP_SIZE と合わせること
	//======================================================================================
	inline constexpr uint32_t PARTICLE_SORT_BLOCK_SIZE = 1024;
	inline constexpr uint32_t PARTICLE_SORT_THREAD_GROUP_SIZE = PARTICLE_SORT_BLOCK_SIZE / 2;

	/// 並べ替え用のバッファの要素数(要る数以上の2のべき乗。BLOCK_SIZE より小さくはしない)
	inline uint32_t ToParticleSortSize(uint32_t a_count)
	{
		return (std::max)(std::bit_ceil(a_count), PARTICLE_SORT_BLOCK_SIZE);
	}

	//======================================================================================
	// 描く一覧の1件 : 描画設定の表の番号と、粒の番号を1つの uint に詰める
	//
	//   上位 12bit : 描画設定の表(ParticleDrawRecord)の番号
	//   下位 20bit : プールの中の粒の番号(1プールの上限 PARTICLE_POOL_HARD_LIMIT = 2^20 に収まる)
	// ※ HLSL 側 PARTICLE_DRAW_* と合わせること
	//======================================================================================
	inline constexpr uint32_t PARTICLE_DRAW_RECORD_SHIFT = 20;
	inline constexpr uint32_t PARTICLE_DRAW_RECORD_MAX = 1u << (32 - PARTICLE_DRAW_RECORD_SHIFT);
	static_assert(PARTICLE_POOL_HARD_LIMIT <= (1u << PARTICLE_DRAW_RECORD_SHIFT), "粒の番号が下位ビットに収まらない");

	// 表の番号を一覧の上位ビットから引く印(半透明をまとめて描くとき、ルート定数で渡す)
	inline constexpr uint32_t PARTICLE_DRAW_RECORD_FROM_LIST = 0xFFFFFFFFu;

	//======================================================================================
	// プールの準備中に溜めておく発生命令の上限(1アセットあたり)
	//
	// 準備中に積まれた命令は、使えるようになるまで持ち越す。
	// 何フレームぶんも溜まって、使えるようになった瞬間に塊で出ないよう、この数で頭打ちにする。
	// (以前はアセットごとの命令バッファの大きさも兼ねていたが、命令バッファは全プール共通の1本になった)
	//======================================================================================
	inline constexpr size_t EMIT_PENDING_REQUEST_MAX = 100;

	//======================================================================================
	// 発生命令バッファ(全プール共通の1本)の大きさ
	//
	// フレームごとに全プールの命令をつなげて送る。足りなくなったら2のべき乗で伸ばし、
	// 作り直した古いバッファは GPU が使い終わってから捨てる(めったに起きない)。
	// 上限を超えたぶんはそのフレームでは出ない(アセットごとに1回だけ警告する)。
	// 1件 160 バイトなので、上限まで伸びても 10MB ほど(+ フレームごとの転送用の区画)
	//======================================================================================
	inline constexpr uint32_t EMIT_BUFFER_MIN_CAPACITY = 256;
	inline constexpr uint32_t EMIT_BUFFER_MAX_CAPACITY = 65536;

	/// <summary>
	/// パーティクルの現在データ
	/// 固定されたデータはアセット側から定数バッファで送る
	/// </summary>
	/// <remarks>
	/// HLSL 側 ParticleData(Common/RootParameters/Particle.hlsli)と並びを合わせること。
	/// GPU のプールはこの sizeof で確保されるので、増やせばそのぶん VRAM を食う
	/// (容量 10000 なら 16 バイト増やして 160KB)。
	/// </remarks>
	struct ParticleData
	{
		Math::Vector3 pos;		// 現在のワールド座標
		float life;					// 残り寿命

		Math::Vector3 velocity;	// 現在の移動ベクトル
		float size;					// サイズ

		// 発生した時点の寿命。
		// 寿命は粒ごとにランダムなので、「今どこまで進んだか(0〜1)」を出すには
		// 割る相手を粒自身が覚えているしかない。サイズ・色・フェードがこれを使う
		float startLife;

		// どの発生源の座標系で回っているか(EmitterSlotPool の席番号)。
		// 0 は単位行列で予約してあるので、ワールド空間で回す粒はここが 0 のまま
		uint32_t emitterIndex;

		// 板ポリを面の中で回す角度(ラジアン)と、その速さ(ラジアン/秒)。
		// Billboard / EmitterAxis / EmitterFacing のときに効く(進行方向に合わせる向きでは使わない)
		float rotation;
		float angularVelocity;

		// 発生したときの発生源の向き(クォータニオン xyzw)。
		// ワールド空間の粒ならワールドの向き、ローカル空間の粒なら席から見た向き。
		// EmitterAxis / EmitterFacing は板の軸をここから取る
		Math::Quaternion orientation;
	};
	static_assert(sizeof(ParticleData) == 64, "HLSL 側 ParticleData と大きさを合わせること");

	//======================================================================================
	// パーティクルをどの座標系で回すか
	//======================================================================================
	/// <remarks>
	/// World : 出した瞬間の位置をワールドに置いて、そのまま飛ばす(従来)。
	///         煙・爆発・弾の軌跡のように「出したその場に残る」もの向け。
	///
	/// Local : 発生源の座標系で回して、描くときにワールドへ戻す。
	///         ブースターの噴射のように「発生源にくっついて動いてほしい」もの向け。
	///         ワールドで回すと、機体が横へ動いた瞬間に噴射だけ置き去りになる。
	///
	/// ※ Local は発生源の位置と回転だけを追う(拡縮は外す)。
	///    追わないと、取り付け側のスケールが粒の飛距離にまで掛かってしまう。
	/// ※ 重力はどちらでもワールドの下向き。Local の粒は席の回転の逆で
	///    席の座標系へ戻してから足す(UpdateParticleShader)。
	/// ※ これはパーティクルアセットの既定値。エフェクトのパーツ側で上書きできる
	///    (EffectParticlePart::simulationSpace)。粒は自分の席番号を持つので、
	///    同じプールの中に Local と World の粒が混ざってよい。
	///
	/// ※ 値は保存されるので、増やすときは必ず末尾に足すこと
	/// </remarks>
	enum class EParticleSimulationSpace : uint32_t
	{
		World,		// ワールド空間で回す(従来)
		Local,		// 発生源の座標系で回して、描画時にワールドへ戻す
	};

	/// <summary>
	/// 板ポリの向きの決め方
	/// ※ 値は保存されるので、増やすときは必ず末尾に足すこと
	/// ※ HLSL 側 PARTICLE_ORIENT_*(Common/RootParameters/Particle.hlsli)と数値を合わせること
	/// </summary>
	enum class EParticleOrientation : uint32_t
	{
		Billboard,			// 常にカメラ正面(従来)
		VelocityBillboard,	// カメラ正面のまま、画面上で進行方向へ回す(火花・破片向き)
		VelocityAxis,		// 進行方向をワールドの縦軸にする(手前へ向かうと縮む。弾道向き)
		EmitterAxis,		// 画像の上を発生源の噴き出す向き(+Z)へ。板は発生源の X-Z 面(炎の舌・噴射の芯向き)
		EmitterFacing,		// 板を発生源の噴き出す向きへ向けて立てる。画像の上は発生源の +Y(衝撃波の輪向き)
	};

	/// <summary>
	/// 発生方向の決め方
	/// </summary>
	/// <remarks>
	/// Cone の角度を 360 にしても全方向にはならない(円錐の半頂角なので、
	/// 全方向にしたければ 180 度で、しかもその極限は分布が偏る)。
	/// 爆発のように四方八方へ飛ばしたいときは Sphere を選ぶこと。
	///
	/// ※ 値は保存されるので、増やすときは必ず末尾に足すこと
	/// ※ HLSL 側 PARTICLE_EMIT_SHAPE_* と数値を合わせること
	/// </remarks>
	enum class EParticleEmitShape : uint32_t
	{
		Cone,			// 指定方向を軸にした円錐(噴射・排気。従来の挙動)
		Sphere,			// 中心から全方向へ均等(爆発)
		Hemisphere,		// 指定方向側の半球だけ(地面での爆発など)
	};

	/// <summary>
	/// 色の重ね方
	/// </summary>
	/// <remarks>
	/// Additive は「光」を足す。重ねるほど明るくなる。火花・炎・爆発の芯はこちら。
	///
	/// AlphaBlend は「物」を前に置く。煙や破片のように光っていないものを
	/// 加算で出すと背景を明るくしてしまうので、そちらはこちらを使う。
	///
	/// どちらも全アセットぶんをまとめてカメラから遠い順に並べ、プリマルチプライドの合成
	/// (色 × 1 + 下地 × (1 - α))で1回で描く(ParticleSimulation が並べ、ParticlePass が描く)。
	///   AlphaBlend : (色 × α, α)を出す。下地は α のぶん隠れる
	///   Additive   : (色 × α, 0)を出す。下地は隠れず、色が足されるだけ
	/// 重ね方が違っても同じ一覧で前後が決まるので、煙の奥にある炎は手前の煙に隠れる
	/// (加算を後からまとめて足していた頃は、煙の奥の炎まで煙の上に乗って見えていた)。
	///
	/// ※ 値は保存されるので、増やすときは必ず末尾に足すこと
	/// </remarks>
	enum class EParticleBlendMode : uint32_t
	{
		Additive,		// 加算合成(従来)
		AlphaBlend,		// 半透明合成
	};

	/// <summary>
	/// 絵を何コマかに割って、コマを送る(フリップブック)かどうか
	/// </summary>
	/// <remarks>
	/// 絵は左上から右へ、行が終わったら次の行へ並べる(FlipbookCols x FlipbookRows)。
	/// 1x1 なら何を選んでもコマ送りしない(従来の1枚絵)。
	///
	/// ※ 値は保存されるので、増やすときは必ず末尾に足すこと
	/// ※ HLSL 側 PARTICLE_FLIPBOOK_* と数値を合わせること
	/// </remarks>
	enum class EParticleFlipbookMode : uint32_t
	{
		None,		// コマ送りしない(1コマ目だけ)
		OverLife,	// 寿命に沿って最初のコマから最後のコマまで送る(火球・煙が膨らんで消える)
		Random,		// 粒ごとにどれか1コマを選んで、そのまま(破片の形を散らす)
	};

	/// <summary>
	/// 絵のチャンネルの読み方
	/// </summary>
	/// <remarks>
	/// Color    : RGBA をそのまま色として掛ける(従来)。
	///
	/// FireSmoke: 火球や煙のように「煙の中で火が燃えて、冷えて煙だけになる」ものを1枚で描く。
	///            R = 煙の明暗、G = 温度、A = 濃さ として読み、
	///              色 = 寿命の色 × R + 発光色 × 熱いところ
	///            熱いところ = 温度(G)が「冷えたしきい値」より上のところ。しきい値は寿命の頭で 0、
	///            EmissiveTime(寿命に対する割合)で 1 まで上がるので、火は外側から冷えて芯だけになり、消える。
	///            半透明で描けば、手前の煙が奥の火を正しく隠す。
	///
	/// ※ 値は保存されるので、増やすときは必ず末尾に足すこと
	/// ※ HLSL 側 PARTICLE_TEX_MODE_* と数値を合わせること
	/// </remarks>
	enum class EParticleTexMode : uint32_t
	{
		Color,			// RGBA をそのまま使う(従来)
		FireSmoke,		// R = 煙の明暗 / G = 温度 / A = 濃さ
	};

	/// <summary>
	/// 描画設定の表の1件(アセット1つぶん)。全アセットぶんを StructuredBuffer に並べて送る
	/// </summary>
	/// <remarks>
	/// 粒はアセットをまたいで1回で描くので、粒ごとに「どのアセットの設定か」を
	/// 表の番号で引く(描く一覧の上位ビット)。並べられなかったフレームだけアセットごとに描き、番号はルート定数で渡す。
	/// ※ HLSL 側 ParticleDrawRecord と並びを合わせること
	/// ※ 発生源の行列は全アセット共通の EmitterSlotPool の StructuredBuffer から引く
	/// </remarks>
	struct ParticleDrawRecord
	{
		uint32_t	particleBufferIndex = 0;	// 粒バッファの SRV の番号
		uint32_t	texIndex = 0;				// 絵の SRV の番号
		uint32_t	orientation = 0;			// EParticleOrientation
		float		stretch = 1.0f;				// 進行方向への伸ばし倍率

		float		endSizeScale = 1.0f;		// 寿命の終わりでのサイズ倍率(1で変化なし)
		float		fadeInRatio = 0.0f;			// 頭から不透明になるまでの割合(0でフェードインなし)
		float		fadeOutRatio = 0.0f;		// 末尾で透明になるまでの割合(0でフェードアウトなし)
		float		softDepth = 0.0f;			// 奥の物との境目をぼかす幅(m。0でぼかさない)

		Math::Color	startColor = { 1.0f, 1.0f, 1.0f, 1.0f };	// 発生時の色
		Math::Color	endColor = { 1.0f, 1.0f, 1.0f, 1.0f };		// 消える直前の色

		// ---- 寿命の途中の色(midColorTime が 0 より大きいときだけ使う) ----
		Math::Color	midColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		float		midColorTime = 0.0f;		// 途中の色になる進み具合(0〜1。0 で使わない = 2色の変化)

		// ---- サイズの変化の曲がり具合 ----
		float		sizeCurvePow = 1.0f;		// 1 で一定の速さ。1 より小さいと頭で一気に膨らむ

		// ---- フリップブック ----
		uint32_t	flipbookCols = 1;			// 横のコマ数
		uint32_t	flipbookRows = 1;			// 縦のコマ数
		uint32_t	flipbookMode = 0;			// EParticleFlipbookMode
		uint32_t	isFlipbookBlend = 0;		// 1 なら隣のコマと混ぜてなめらかに送る

		// ---- 絵のチャンネルの読み方 ----
		uint32_t	texMode = 0;				// EParticleTexMode
		float		emissiveTime = 0.0f;		// FireSmoke : 寿命のこの割合で火が消える(0 で光らない)
		Math::Color	emissiveColor = { 0.0f, 0.0f, 0.0f, 1.0f };	// FireSmoke : 熱いところの発光色(1 を超えてよい)

		// ---- 色の重ね方 ----
		// 加算も半透明も同じ PSO(プリマルチプライド)で描くので、PS が出す α をこれで変える
		uint32_t	blendMode = 0;				// EParticleBlendMode
		uint32_t	pad[3] = {};
	};
	static_assert(sizeof(ParticleDrawRecord) == 144, "HLSL 側 ParticleDrawRecord と大きさを合わせること");
}
