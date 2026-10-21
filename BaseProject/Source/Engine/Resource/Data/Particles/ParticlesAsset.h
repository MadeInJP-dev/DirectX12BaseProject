#pragma once

#include "Engine/Graphics/Particle/Core/ParticleData.h"

namespace Engine::Resource
{

	/// <summary>
	/// パーティクルアセット : 
	/// データとして保存される。
	/// インスタンスからの参照を受けるため１パーティクルにつき一つ
	/// </summary>
	class ParticlesAsset
	{
	public:
		ParticlesAsset() = default;
		~ParticlesAsset() = default;
		NON_COPYABLE_MOVABLE(ParticlesAsset);

		// 作成処理 : 名前だけを決める。
		// GUIDはメタファイルを作る AssetDatabase の持ち物なので、読み込み時に引き直す
		void Create(const std::string& a_name);

		// 解放処理
		void Release();

		// シリアライズ
		void Save(const std::string& a_filePath);
		// a_resourceManager : 参照テクスチャを読み込む先
		void Load(const std::string& a_fileDir, const std::string& a_fileName, ResourceManager& a_resourceManager);
		void Load(const std::string& a_filePath, ResourceManager& a_resourceManager);

		// ---- アクセサ ----
		const std::string& GetName()const { return m_name; }				// パーティクル名
		// 中身に書き込まれているGUID。
		// 新規作成直後のファイルでは空のまま(GUIDは監視がメタファイルへ発行する)なので、
		// 保存先を引く用途には使わず、呼び出し側が持っているGUIDを使うこと
		const Core::GUID& GetGUID() const { return m_guid; }
		const Core::GUID& GetTexGUID() const { return m_texGUID; }		// テクスチャGUID
		Handle<Texture> GetTexHandle() const { return m_texHandle; }		// テクスチャハンドル
		float GetInitalSpeedMin() const { return m_initialSpeedMin; }		// 最小初速
		float GetInitalSpeedMax() const { return m_initialSpeedMax; }		// 最大初速
		float GetGravityPow() const { return m_gravityPow; }				// 重力影響度
		float GetLifeTimeMin() const { return m_lifeTimeMin; }				// 最小生存時間
		float GetLifeTimeMax() const { return m_lifeTimeMax; }				// 最大生存時間
		int GetCapacity() const { return m_capacity; }						// 最初に用意しておく粒の数(足りなければプールが伸びる)
		int GetEmissionRate() const { return m_emissionRate; }				// 発生レート
		Graphics::Particle::EParticleOrientation GetOrientation() const { return m_orientation; }	// 板ポリの向き
		float GetStretch() const { return m_stretch; }						// 進行方向への伸ばし倍率
		float GetDrag() const { return m_drag; }							// 速度の減衰
		float GetEndSizeScale() const { return m_endSizeScale; }			// 寿命の終わりでのサイズ倍率
		const Math::Color& GetStartColor() const { return m_startColor; }	// 発生時の色
		const Math::Color& GetEndColor() const { return m_endColor; }		// 消える直前の色
		float GetFadeInRatio() const { return m_fadeInRatio; }				// フェードインの割合
		float GetFadeOutRatio() const { return m_fadeOutRatio; }			// フェードアウトの割合
		Graphics::Particle::EParticleBlendMode GetBlendMode() const { return m_blendMode; }	// 色の重ね方
		Graphics::Particle::EParticleSimulationSpace GetSimulationSpace() const { return m_simulationSpace; }	// どの座標系で回すか
		bool IsLocalSpace() const { return m_simulationSpace == Graphics::Particle::EParticleSimulationSpace::Local; }
		float GetRotationMin() const { return m_rotationMin; }					// 板の初期角の下限(度)
		float GetRotationMax() const { return m_rotationMax; }					// 板の初期角の上限(度)
		float GetAngularVelocityMin() const { return m_angularVelocityMin; }	// 板の回転速度の下限(度/秒)
		float GetAngularVelocityMax() const { return m_angularVelocityMax; }	// 板の回転速度の上限(度/秒)
		int GetSortOrder() const { return m_sortOrder; }						// 同じ重ね方どうしの描く順(小さいほど先 = 下)
		float GetSoftDepth() const { return m_softDepth; }						// 奥の物との境目をぼかす幅(m)。0 でぼかさない
		const Math::Color& GetMidColor() const { return m_midColor; }			// 寿命の途中の色
		float GetMidColorTime() const { return m_midColorTime; }				// 途中の色になる進み具合(0 で使わない)
		float GetSizeCurvePow() const { return m_sizeCurvePow; }				// サイズの変化の曲がり具合
		int GetFlipbookCols() const { return m_flipbookCols; }					// フリップブックの横のコマ数
		int GetFlipbookRows() const { return m_flipbookRows; }					// フリップブックの縦のコマ数
		Graphics::Particle::EParticleFlipbookMode GetFlipbookMode() const { return m_flipbookMode; }	// コマの送り方
		bool IsFlipbookBlend() const { return m_isFlipbookBlend; }				// 隣のコマと混ぜるか
		Graphics::Particle::EParticleTexMode GetTexMode() const { return m_texMode; }	// 絵のチャンネルの読み方
		float GetEmissiveTime() const { return m_emissiveTime; }				// FireSmoke : 火が消える進み具合
		const Math::Color& GetEmissiveColor() const { return m_emissiveColor; }	// FireSmoke : 熱いところの発光色

		// ---- 編集用アクセサ : エディターから直接書き換えるためのもの ----
		std::string& RefName() { return m_name; }
		float& RefInitalSpeedMin() { return m_initialSpeedMin; }
		float& RefInitalSpeedMax() { return m_initialSpeedMax; }
		float& RefGravityPow() { return m_gravityPow; }
		float& RefLifeTimeMin() { return m_lifeTimeMin; }
		float& RefLifeTimeMax() { return m_lifeTimeMax; }
		int& RefCapacity() { return m_capacity; }
		int& RefEmissionRate() { return m_emissionRate; }
		Graphics::Particle::EParticleOrientation& RefOrientation() { return m_orientation; }
		float& RefStretch() { return m_stretch; }
		float& RefDrag() { return m_drag; }
		float& RefEndSizeScale() { return m_endSizeScale; }
		Math::Color& RefStartColor() { return m_startColor; }
		Math::Color& RefEndColor() { return m_endColor; }
		float& RefFadeInRatio() { return m_fadeInRatio; }
		float& RefFadeOutRatio() { return m_fadeOutRatio; }
		Graphics::Particle::EParticleBlendMode& RefBlendMode() { return m_blendMode; }
		Graphics::Particle::EParticleSimulationSpace& RefSimulationSpace() { return m_simulationSpace; }
		float& RefRotationMin() { return m_rotationMin; }
		float& RefRotationMax() { return m_rotationMax; }
		float& RefAngularVelocityMin() { return m_angularVelocityMin; }
		float& RefAngularVelocityMax() { return m_angularVelocityMax; }
		int& RefSortOrder() { return m_sortOrder; }
		float& RefSoftDepth() { return m_softDepth; }
		Math::Color& RefMidColor() { return m_midColor; }
		float& RefMidColorTime() { return m_midColorTime; }
		float& RefSizeCurvePow() { return m_sizeCurvePow; }
		int& RefFlipbookCols() { return m_flipbookCols; }
		int& RefFlipbookRows() { return m_flipbookRows; }
		Graphics::Particle::EParticleFlipbookMode& RefFlipbookMode() { return m_flipbookMode; }
		bool& RefIsFlipbookBlend() { return m_isFlipbookBlend; }
		Graphics::Particle::EParticleTexMode& RefTexMode() { return m_texMode; }
		float& RefEmissiveTime() { return m_emissiveTime; }
		Math::Color& RefEmissiveColor() { return m_emissiveColor; }

		// テクスチャの差し替え : GUIDとハンドルを同時に更新する
		void SetTexture(const Core::GUID& a_guid, const ResourceRef<Texture>& a_handle)
		{
			m_texGUID = a_guid;
			m_texHandle = a_handle;
		}


	private:

		/// <summary>
		/// 保存と読み込みで共通の項目並び
		/// </summary>
		/// <remarks>
		/// 以前は Save と Load 2つに同じ並びを3回書いていて、
		/// 項目を足すときに片方だけ直すと読み書きがずれた。1箇所に寄せてある。
		/// ※ 追加は末尾に。バイナリは順次読みなので途中に挿すと既存データが全部ずれる
		/// </remarks>
		void Archive(Persistence::Archive& a_ar);

		/// <summary>
		/// 読み込み後の後始末(値の下限補正とテクスチャの解決)
		/// </summary>
		void OnLoaded(ResourceManager& a_resourceManager);

	private:

		// ---- 識別子 ----
		std::string m_name;		// アセット名
		Core::GUID m_guid;

		// ---- 静的データ ----
		// 参照データ
		Core::GUID m_texGUID;		// テクスチャ

		// 初速
		float m_initialSpeedMin = 1.0f;
		float m_initialSpeedMax = 5.0f;

		// 重力からの影響度。
		// 1 で普通に落ち、0 で無重力、負にすると浮き上がる(煙・炎向き)
		float m_gravityPow = 0.0f;

		// 生存時間
		float m_lifeTimeMin = 0.5f;
		float m_lifeTimeMax = 2.0f;

		// 最初に用意しておく粒の数(目安)。
		// 以前は上限だったが、今はプールが足りなくなったら自動で伸びる(PARTICLE_POOL_BLOCK_SIZE 単位)。
		// 大きく伸びたプールは、眠ってしばらくするとこの数まで縮む。保存のキーは m_capacity のまま
		int m_capacity = 10000;

		// 発生レート / s
		int m_emissionRate = 0;

		// 板ポリの向き : 進行方向へ画像を回すかどうか
		Graphics::Particle::EParticleOrientation m_orientation = Graphics::Particle::EParticleOrientation::Billboard;

		// 進行方向への伸ばし倍率 : 1 で伸ばさない。速い火花や弾道を線にしたいときに上げる
		// (Billboard 指定のときは使わない)
		float m_stretch = 1.0f;

		//----------------------------------------------------------------------------------
		// 寿命に沿った変化
		//
		// 爆発は「速く飛び出して失速し、膨らみながら白→オレンジ→煙へ落ちて消える」。
		// 初速と寿命だけでは等速で飛び続けて唐突に消えるので、その間を埋めるための設定。
		//----------------------------------------------------------------------------------

		// 速度の減衰(1秒あたりの割合)。0 で減衰なし。
		// 大きいほど早く失速する。爆発の破片は 2〜5 あたりが目安
		float m_drag = 0.0f;

		// 寿命の終わりでのサイズ倍率。1 で変化なし。
		// 煙のように膨らむなら 1 より大きく、火花のように縮むなら小さく
		float m_endSizeScale = 1.0f;

		// 発生時と消える直前の色。
		// RGB は 1 を超えてよく、超えたぶんがブルームのしきい値を抜けて光って見える
		// (爆発の芯を白く飛ばしたいときは startColor を 3〜10 くらいに上げる)
		Math::Color m_startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		Math::Color m_endColor = { 1.0f, 1.0f, 1.0f, 1.0f };

		// 寿命に対する割合でのフェード。0 でそのフェードをしない。
		// 割合で持つので、粒ごとに寿命がばらついても見え方が揃う
		float m_fadeInRatio = 0.0f;
		float m_fadeOutRatio = 0.25f;

		// 色の重ね方。光り物は加算、煙や破片は半透明。
		// 既定を加算にしてあるのは、これを入れる前が加算固定だったため
		// (既存のアセットの見え方を変えない)
		Graphics::Particle::EParticleBlendMode m_blendMode = Graphics::Particle::EParticleBlendMode::Additive;

		// どの座標系で回すか。
		// 既定をワールドにしてあるのは、これを入れる前がワールド固定だったため
		// (既存のアセットの見え方を変えない)。
		// ブースターの噴射のように発生源へくっついてほしいものは Local にする
		Graphics::Particle::EParticleSimulationSpace m_simulationSpace = Graphics::Particle::EParticleSimulationSpace::World;

		//----------------------------------------------------------------------------------
		// 板を面の中で回す
		//
		// 初期角は粒ごとに区間の乱数。回転速度も区間の乱数で、負なら逆回り。
		// どちらも既定は 0(回さない)なので、これを入れる前のアセットの見え方は変わらない。
		// Billboard / EmitterAxis / EmitterFacing のときに効く(進行方向に合わせる向きでは使わない)
		//----------------------------------------------------------------------------------
		float m_rotationMin = 0.0f;			// 度
		float m_rotationMax = 0.0f;			// 度
		float m_angularVelocityMin = 0.0f;	// 度/秒
		float m_angularVelocityMax = 0.0f;	// 度/秒

		// 同じ重ね方(加算/半透明)どうしで描く順。小さいほど先に描く(= 下になる)。
		// 粒は深度を書かないので、後から描いたものが上に乗る。
		// 同じ値なら名前順。重ね方そのものの順(半透明 → 加算)はこれより優先される
		int m_sortOrder = 0;

		// 奥にある物(地面・壁)との境目をぼかす幅(m)。0 でぼかさない。
		// 粒は板なので、地面へ刺さると交わったところで絵がまっすぐ切れて見える。
		// 板と奥の物の距離がこの幅より近いところほど薄くする(ソフトパーティクル)。
		// 地面を這う霧や大きな煙のように、板が物へ刺さるものに使う。
		// 既定は 0 なので、これを入れる前のアセットの見え方は変わらない
		float m_softDepth = 0.0f;

		//----------------------------------------------------------------------------------
		// 寿命の途中の色
		//
		// 開始と終わりの2色だけだと、火の色から煙の色へ変わる途中が濁った中間色になる。
		// 途中の色を置くと 開始 → 途中 → 終わり の3色で変えられる。
		// 進み具合が 0(既定)なら使わないので、これを入れる前のアセットの見え方は変わらない
		//----------------------------------------------------------------------------------
		Math::Color m_midColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		float m_midColorTime = 0.0f;

		// サイズの変化の曲がり具合。サイズ倍率 = lerp(1, endSizeScale, 進み具合 ^ これ)。
		// 1(既定)で一定の速さ。1 より小さいと頭で一気に膨らんで、あとはゆっくりになる(爆風・煙)
		float m_sizeCurvePow = 1.0f;

		//----------------------------------------------------------------------------------
		// フリップブック : 絵を cols x rows のコマに割ってコマを送る
		//
		// 1x1(既定)ならコマ送りしないので、これを入れる前のアセットの見え方は変わらない。
		// 寿命に沿って送るものは、隣のコマと混ぜるとコマ数が少なくてもなめらかに動く
		//----------------------------------------------------------------------------------
		int m_flipbookCols = 1;
		int m_flipbookRows = 1;
		Graphics::Particle::EParticleFlipbookMode m_flipbookMode = Graphics::Particle::EParticleFlipbookMode::None;
		bool m_isFlipbookBlend = true;

		//----------------------------------------------------------------------------------
		// 絵のチャンネルの読み方(EParticleTexMode)
		//
		// FireSmoke は R = 煙の明暗、G = 温度、A = 濃さ。色 = 寿命の色 × R + 発光色 × 熱いところ。
		// 火は寿命の頭で全体が光り、emissiveTime(寿命に対する割合)までに外側から冷えて消える。
		// 既定は Color(従来)なので、これを入れる前のアセットの見え方は変わらない
		//----------------------------------------------------------------------------------
		Graphics::Particle::EParticleTexMode m_texMode = Graphics::Particle::EParticleTexMode::Color;
		float m_emissiveTime = 0.0f;
		Math::Color m_emissiveColor = { 0.0f, 0.0f, 0.0f, 1.0f };

		// ---- ランタイム用データ ----
		ResourceRef<Texture> m_texHandle;
	};
}