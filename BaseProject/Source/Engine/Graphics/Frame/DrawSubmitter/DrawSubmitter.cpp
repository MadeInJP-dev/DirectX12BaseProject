#include "Engine/Graphics/Frame/DrawSubmitter/DrawSubmitter.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Device/RenderDevice.h"
#include "Engine/Graphics/Frame/MeshBufferAllocator/MeshBufferAllocator.h"
#include "Engine/Graphics/PipelineState/PipelineStateManager/PipelineStateManager.h"
#include "Engine/Graphics/RenderingPipeline/CameraPipelineManager/CameraPipelineManager.h"
#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/ECS/World/World.h"
#include "Engine/Graphics/Frame/DrawList/UIData.h"

namespace Engine::Graphics
{
	namespace
	{
		// 文字列を行へ切る : 改行(\n)で区切り、復帰(\r)は捨てる
		std::vector<std::vector<uint32_t>> SplitLines(std::string_view a_utf8Text)
		{
			std::vector<std::vector<uint32_t>> _lines = { {} };

			for (const uint32_t _codePoint : Core::String::ToCodePoints(a_utf8Text))
			{
				if (_codePoint == '\r') continue;		// 復帰は送りを持たない
				if (_codePoint == '\n')
				{
					_lines.emplace_back();
					continue;
				}
				_lines.back().push_back(_codePoint);
			}

			return _lines;
		}

		// 1行ぶんの幅(px, フォント基準サイズ)を測る
		float MeasureLine(
			Resource::Font& a_font,
			const std::vector<uint32_t>& a_line,
			float a_charSpacingInFontUnit)
		{
			float _width = 0.0f;
			uint32_t _prev = 0;

			for (const uint32_t _codePoint : a_line)
			{
				const Resource::Glyph* _pGlyph = a_font.RequestGlyph(_codePoint);
				if (_pGlyph == nullptr) continue;

				if (_prev != 0) _width += a_font.GetKerning(_prev, _codePoint);
				_width += _pGlyph->xAdvance + a_charSpacingInFontUnit;

				_prev = _codePoint;
			}

			return _width;
		}

		// 度で回す : UIAnchor::IsPointInside と同じ向き(時計回り)
		Math::Vector2 RotateDeg(const Math::Vector2& a_value, float a_degree)
		{
			if (a_degree == 0.0f) return a_value;

			const float _rad = DirectX::XMConvertToRadians(a_degree);
			const float _cos = std::cos(_rad);
			const float _sin = std::sin(_rad);

			return {
				a_value.x * _cos - a_value.y * _sin,
				a_value.x * _sin + a_value.y * _cos
			};
		}
	}

	void DrawSubmitter::Init(GraphicsEngine* a_pGraphicsEngine)
	{
		// 積み込みは1フレームに何万回も通るので、引く先はここで控えておく。
		// どれも GraphicsEngine の持ち物で、このクラスより長生きする
		m_pDrawLists = a_pGraphicsEngine->RefDrawLists();
		m_pResourceManager = a_pGraphicsEngine->RefResourceManager();
		m_pPipelineStateManager = a_pGraphicsEngine->RefPipelineStateManager();
		m_pMeshBufferAllocator = a_pGraphicsEngine->RefMeshBufferAllocator();
		m_pCameraPipelines = a_pGraphicsEngine->RefCameraPipelines();
		m_pRenderDevice = a_pGraphicsEngine->RefRenderDevice();
		m_renderWidth = a_pGraphicsEngine->GetRenderWidth();
		m_renderHeight = a_pGraphicsEngine->GetRenderHeight();
	}

	void DrawSubmitter::SubmitSkinning(const GPUSkiningSubmitDesc& a_desc)
	{
		if (!a_desc.pWorld) return;
		auto& _world = *a_desc.pWorld;

		const auto* _pModel = (*m_pResourceManager).Get(a_desc.modelHandle);
		if (!_pModel) return;

		// このワールドのボーン行列をパレットへ積み、GPU上の土台を得る
		const uint32_t _boneBaseIndex = m_pDrawLists->AcquireBoneBaseIndex(_world);

		const auto& _drawCmdVec = _pModel->GetDrawCommandVec();
		for (const auto& _cmd : _drawCmdVec)
		{
			// マテリアル取得
			auto* _pMaterial = (*m_pResourceManager).Get(_cmd.materialHandle);
			if (!_pMaterial) continue;

			// メッシュ取得
			auto* _pMesh = (*m_pResourceManager).Get(_cmd.meshHandle);
			if (!_pMesh) continue;

			// レイトレ用データを持たないメッシュはスキニング登録できない
			if (!_pMesh->HasRtData()) continue;

			SkinningDispatchItem _item = {};
			_item.pWorld = &_world;
			_item.staticVertexHandle = _pMesh->GetRtData().vertexHandle;
			_item.skinVertexHandle = _pMesh->GetRtData().skinVertexHandle;
			_item.staticIndexHandle = _pMesh->GetRtData().indexHandle;
			_item.nodePoseMat = a_desc.nodeposeHandle;
			_item.animHandle = a_desc.rtAnimHandle;
			_item.boneHandle = a_desc.boneHandle;

			// スキニングのコンピュートが読むのはGPU上の位置なので土台を足す
			_item.boneBufferStart = _boneBaseIndex + a_desc.boneHandle.startIndex;

			auto& _pool = _world.RefResource<Pool::ItemPool<Raytracing::DynamicRaytracingData>>();
			auto* _data = _pool.Get(a_desc.rtAnimHandle);
			if (!_data) continue;

			for (auto& _meshData : _data->meshDataVec)
			{
				if (_cmd.meshHandle == _meshData.meshHandle)
				{
					_item.animatedHandle = _meshData.animatedVertexHandle;
				}
			}

			m_pDrawLists->AddSkinning(_item);
		}
	}

	void DrawSubmitter::SubmitModel(const ModelSubmitDesc& a_desc)
	{
		const auto* _pModel = (*m_pResourceManager).Get(a_desc.modelHandle);
		if (!_pModel) return;

		// 過去行列が無ければ止まっているものとして今の行列を使う
		const Math::Matrix& _prevWorldMat = a_desc.prevWorldMat ? *a_desc.prevWorldMat : a_desc.worldMat;

		// LOD : モデル全体を囲む球をワールドへ移しておく。
		// どのサブセットも同じ球で段を選ぶので、1体の中で段が食い違わない
		const bool _hasLod = !_pModel->GetLodModels().empty();
		Math::Vector4 _worldSphere = {};
		if (_hasLod)
		{
			DirectX::BoundingSphere _sphere = {};
			_pModel->GetLocalBounds().Transform(_sphere, Math::DX::Load(a_desc.worldMat));
			_worldSphere = Math::Vector4(_sphere.Center.x, _sphere.Center.y, _sphere.Center.z, _sphere.Radius);
		}

		// モデルが持っている描画コマンド（サブセット）を展開
		const auto& _drawCmdVec = _pModel->GetDrawCommandVec();

		for (size_t _cmdIndex = 0; _cmdIndex < _drawCmdVec.size(); ++_cmdIndex)
		{
			const auto& _cmd = _drawCmdVec[_cmdIndex];

			// -----------------------------------------------------
			// リソースの取得と検証
			// -----------------------------------------------------
			const Resource::Mesh* _pMesh = nullptr;
			const Resource::Material* _pMaterial = nullptr;
			if (!FetchDrawResources(_cmd, _pMesh, _pMaterial)) continue;

			// -----------------------------------------------------
			// 行列計算
			// -----------------------------------------------------
			Math::Matrix _nodeTransMat(_pModel->GetOriginalNodeVec()[_cmd.nodeIndex].worldTransform);
			Math::Matrix _mat = _nodeTransMat * a_desc.worldMat;
			Math::Matrix _prevMat = _nodeTransMat * _prevWorldMat;

			// -----------------------------------------------------
			// PermutationFlags の構築
			// -----------------------------------------------------
			// この経路は静的モデル専用(アニメーションするモデルはボーンを受け取る方の SubmitModel)
			constexpr bool IS_ANIMATION = false;
			uint32_t _flags = static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::None);
			_flags |= static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::Static);

			if (_cmd.alphaMode == Engine::Resource::EAlpha::Mask) {
				_flags |= static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::AlphaMasked);
			}

			Engine::Graphics::PSOKey _psoKey = {};
			_psoKey.permutationFlags = _flags;

			// -----------------------------------------------------
			// LOD の段 : どの段で描くかは増幅シェーダーがパスごとに決める
			// -----------------------------------------------------
			uint32_t _lodStart = 0;
			uint32_t _lodCount = 0;
			if (_hasLod)
			{
				PushMeshLodData(*_pModel, _cmdIndex, _cmd, _pMesh, _worldSphere, _lodStart, _lodCount);
			}

			// -----------------------------------------------------
			// 各パスへの描画アイテム登録(共通処理)
			// -----------------------------------------------------
			RegisterDrawCommandToPasses(
				_cmd, _pMesh, _pMaterial,
				_mat, _prevMat,
				IS_ANIMATION, 0 /*animatedVertexStart*/,
				a_desc.colorScale, a_desc.emissiveScale, a_desc.bloomAdd, _psoKey,
				_lodStart, _lodCount, a_desc.fade);
		}
	}

	void DrawSubmitter::SubmitModel(const AnimationModelSubmitDesc& a_desc)
	{
		if (!a_desc.pWorld) return;
		auto& _world = *a_desc.pWorld;

		const auto* _pModel = (*m_pResourceManager).Get(a_desc.modelHandle);
		if (!_pModel) return;

		// 過去行列が無ければ止まっているものとして今の行列を使う
		const Math::Matrix& _prevWorldMat = a_desc.prevWorldMat ? *a_desc.prevWorldMat : a_desc.worldMat;

		// ノード行列取得
		auto& _nodePosePool = _world.RefResource<Pool::RangePool<Resource::NodePoseMatrix>>();
		const auto& _nodePoseMatVec = _nodePosePool.GetRange(a_desc.nodeposeHandle);

		// アニメーション後データ (※1つのモデルに対して共通ならループ外で取得・チェックすると効率的です)
		auto& _pool = _world.RefResource<Pool::ItemPool<Raytracing::DynamicRaytracingData>>();
		auto* _data = _pool.Get(a_desc.rtAnimHandle);
		if (!_data) return;

		// このワールドのボーン行列をパレットへ積む。
		// 戻り値の土台位置はここでは使わない(頂点をスキニングするのはコンピュートの
		// スキニングパスで、描画側はその結果の頂点バッファを読むだけ)。
		// ただし積むこと自体はそのパスに要るので、呼び出しを外してはいけない
		m_pDrawLists->AcquireBoneBaseIndex(_world);

		// モデルが持っている描画コマンド（サブセット）を展開
		const auto& _drawCmdVec = _pModel->GetDrawCommandVec();
		for (const auto& _cmd : _drawCmdVec)
		{
			// メッシュ・マテリアルの取得と検証
			const Resource::Mesh* _pMesh = nullptr;
			const Resource::Material* _pMaterial = nullptr;
			if (!FetchDrawResources(_cmd, _pMesh, _pMaterial)) continue;

			// -----------------------------------------------------
			// アニメーション用頂点オフセットの検索
			// -----------------------------------------------------
			uint32_t _animatedVertexStart = 0;
			if (_data)
			{
				for (const auto& _meshData : _data->meshDataVec)
				{
					if (_cmd.meshHandle == _meshData.meshHandle)
					{
						_animatedVertexStart = _meshData.animatedVertexHandle.startIndex;
						break; // 見つかったらループを抜ける
					}
				}
			}

			// ノードのワールド行列を確定
			// モデル差し替え直後などで描画コマンドとポーズ領域のサイズが食い違った場合は
			// クラッシュさせずこのコマンドの描画をスキップする
			if (_cmd.nodeIndex >= _nodePoseMatVec.size()) continue;
			Math::Matrix _nodeTransMat(_nodePoseMatVec[_cmd.nodeIndex].world);
			Math::Matrix _mat = _nodeTransMat * a_desc.worldMat;

			Math::Matrix _prevMat = _nodeTransMat * _prevWorldMat;

			// =========================================================
			// PermutationFlags を構築
			// =========================================================
			uint32_t _flags = static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::None);

			// アニメーション判定
			bool _isAnimation = (a_desc.boneHandle.count > 0);
			_flags |= static_cast<uint32_t>(_isAnimation ?
				Engine::Graphics::EShaderPermutationFlags::Skinned :
				Engine::Graphics::EShaderPermutationFlags::Static);

			// アルファモード判定
			if (_cmd.alphaMode == Engine::Resource::EAlpha::Mask) {
				_flags |= static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::AlphaMasked);
			}

			// PSOKey作成
			Engine::Graphics::PSOKey _psoKey = {};
			_psoKey.permutationFlags = _flags;

			// =========================================================
			// 各パスへ描画アイテムを投げる(共通処理)
			// =========================================================
			RegisterDrawCommandToPasses(
				_cmd, _pMesh, _pMaterial,
				_mat, _prevMat,
				_isAnimation, _animatedVertexStart,
				a_desc.colorScale, a_desc.emissiveScale, a_desc.bloomAdd, _psoKey,
				0 /*lodStart*/, 0 /*lodCount*/, a_desc.fade);
		}
	}

	void DrawSubmitter::SubmitGroundModel(const GroundModelSubmitDesc& a_desc)
	{
		const auto* _pModel = (*m_pResourceManager).Get(a_desc.modelHandle);
		if (!_pModel) return;

		// モデルが持っている描画コマンド（サブセット）を展開
		const auto& _drawCmdVec = _pModel->GetDrawCommandVec();

		for (const auto& _cmd : _drawCmdVec)
		{
			// -----------------------------------------------------
			// リソースの取得と検証
			// -----------------------------------------------------
			const Resource::Mesh* _pMesh = nullptr;
			const Resource::Material* _pMaterial = nullptr;
			if (!FetchDrawResources(_cmd, _pMesh, _pMaterial)) continue;

			// -----------------------------------------------------
			// 行列計算
			// -----------------------------------------------------
			Math::Matrix _nodeTransMat(_pModel->GetOriginalNodeVec()[_cmd.nodeIndex].worldTransform);
			Math::Matrix _mat = _nodeTransMat * a_desc.worldMat;
			Math::Matrix _prevMat = _nodeTransMat * a_desc.worldMat;		// 動くことはない予定

			// -----------------------------------------------------
			// PermutationFlags の構築
			// -----------------------------------------------------
			// この経路は静的モデル専用(アニメーションするモデルはボーンを受け取る方の SubmitModel)
			constexpr bool IS_ANIMATION = false;
			uint32_t _flags = static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::None);
			_flags |= static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::Static);

			if (_cmd.alphaMode == Engine::Resource::EAlpha::Mask) {
				_flags |= static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::AlphaMasked);
			}

			Engine::Graphics::PSOKey _psoKey = {};
			_psoKey.permutationFlags = _flags;

			// -----------------------------------------------------
			// 各パスへの描画アイテム登録(共通処理)
			// -----------------------------------------------------
			RegisterDrawCommandToPasses(
				EGeometryQueue::Ground,
				_cmd, _pMesh, _pMaterial,
				_mat, _prevMat,
				IS_ANIMATION, 0 /*animatedVertexStart*/,
				{}, {}, {}, _psoKey);
		}
	}

	//------------------------------------------------------------------------------------------
	// 境界面を1枚積む
	//
	// モデルと違ってパスごとのPSOも描画アイテムも作らない。
	// 描くパスは BoundaryPass だけなので、面の情報をそのまま積んでおき、
	// パスが1枚ずつ定数バッファへ詰めて描く
	//------------------------------------------------------------------------------------------
	void DrawSubmitter::SubmitBoundary(const BoundarySubmitDesc& a_desc)
	{
		// 画像が読めていなければ、そのフレームは描かない(白い板が出ないように)
		const int _texIndex = GetSRVIndexFromTextureHandle(a_desc.texHandle);
		if (_texIndex < 0) return;

		BoundaryDrawData _data = {};
		_data.worldMat = a_desc.worldMat;
		_data.color = Math::DX::ToVector4(a_desc.color);
		_data.revealPos = a_desc.revealPos;
		_data.revealStartDistance = a_desc.revealStartDistance;
		_data.revealEndDistance = a_desc.revealEndDistance;
		_data.uvTiling = a_desc.uvTiling;
		_data.uvScroll = a_desc.uvScroll;
		_data.texIndex = static_cast<UINT>(_texIndex);
		_data.time = a_desc.time;
		_data.scanLineDensity = a_desc.scanLineDensity;
		_data.opacityScanLine = a_desc.opacityScanLine;
		_data.opacityNoise = a_desc.opacityNoise;
		_data.flickeringSpeed = a_desc.flickeringSpeed;
		_data.flickeringStrength = a_desc.flickeringStrength;

		m_pDrawLists->AddBoundary(_data);
	}

	void DrawSubmitter::SubmitUI(const SpriteSubmitDesc& a_desc)
	{
		auto& _resourceManager = (*m_pResourceManager);

		// 読み込みが終わっていないものは、そのフレームは描かない。
		// 非同期ロード中のスロットには空の実体が入っているため、
		// ポインタのnullチェックだけでは弾けない
		if (!_resourceManager.IsReady(a_desc.texHandle)) return;

		auto* _pTex = _resourceManager.Get(a_desc.texHandle);
		if (!_pTex) return;

		// 効果のノイズ : 指定が無い・まだ届いていないときはシェーダー内のノイズで代わりにする
		// (届くまで描かないと、ディゾルブの最中にUIごと消えてしまうため)
		uint32_t _noiseTexIndex = UI_NOISE_TEX_NONE;
		if (a_desc.effect.noiseTexHandle.IsValid() && _resourceManager.IsReady(a_desc.effect.noiseTexHandle))
		{
			if (const auto* _pNoise = _resourceManager.Get(a_desc.effect.noiseTexHandle))
			{
				_noiseTexIndex = _pNoise->GetSRV().GetIndex();
			}
		}

		// サイズは呼び出し側の指定値をそのまま使う
		PushUIData(_pTex->GetSRV().GetIndex(), _noiseTexIndex, a_desc);
	}

	//------------------------------------------------------------------------------------------
	// 文字列を1文字ずつUIとして積む
	//
	// グリフの位置は pixelPos からのずれ(回転前)で組み、ずれを回してから積む。
	// クアッド自体も同じ角度で回すので、文字列全体が1枚として回る。
	// 湾曲も文字ごとに「弧の頂点からの中心のずれ」を渡すので、全部の文字が同じ1本の弧に乗る
	//------------------------------------------------------------------------------------------
	void DrawSubmitter::SubmitString(const FontSubmitDesc& a_desc)
	{
		if (a_desc.text.empty()) return;
		if (a_desc.color.a <= 0.0f) return;

		auto& _resourceManager = (*m_pResourceManager);

		// 読み込みが終わっていないものは、そのフレームは描かない
		if (!a_desc.fontHandle.IsValid()) return;
		if (!_resourceManager.IsReady(a_desc.fontHandle)) return;

		// グリフは要求された時点で焼くので、参照は書き込み可能で引く
		Resource::Font* _pFont = _resourceManager.Ref(a_desc.fontHandle);
		if (_pFont == nullptr || !_pFont->IsValid()) return;

		const float _atlasSize = static_cast<float>(_pFont->GetAtlasSize());
		if (_atlasSize <= 0.0f) return;

		// 基準サイズ(64px)で焼いたものを、出したい大きさへ縮小する
		const float _fontScale = _pFont->GetScaleForSize(a_desc.fontPixelSize);
		if (_fontScale <= 1e-6f) return;

		// 字間はピクセル指定なので、測るときはフォント基準サイズへ戻して足す
		const float _charSpacingInFontUnit = a_desc.charSpacing / _fontScale;

		const auto _lines = SplitLines(a_desc.text);

		const float _lineHeight = _pFont->GetLineHeight() * a_desc.lineSpacing;
		const float _ascent = _pFont->GetAscent();

		// ブロック全体の大きさ : ピボットを当てる基準になる
		float _blockWidth = 0.0f;
		std::vector<float> _lineWidthVec;
		_lineWidthVec.reserve(_lines.size());
		for (const auto& _line : _lines)
		{
			const float _lineWidth = MeasureLine(*_pFont, _line, _charSpacingInFontUnit);
			_lineWidthVec.push_back(_lineWidth);
			_blockWidth = std::max(_blockWidth, _lineWidth);
		}
		const float _blockHeight = _lineHeight * static_cast<float>(_lines.size());

		// ピボットはブロック全体に対して効かせる
		const Math::Vector2 _blockTopLeft = {
			-a_desc.pivot.x * _blockWidth * _fontScale,
			-a_desc.pivot.y * _blockHeight * _fontScale
		};

		// 1文字ぶんの描画命令 : 文字ごとに変わらないものはここで入れておく
		SpriteSubmitDesc _glyphDesc = {};
		_glyphDesc.texHandle = _pFont->GetAtlasTextureHandle();
		_glyphDesc.rotationDeg = a_desc.rotationDeg;
		_glyphDesc.Zlayer = a_desc.Zlayer;
		_glyphDesc.color = a_desc.color;
		_glyphDesc.pivot = { 0.0f, 0.0f };		// ずれで位置を決めているのでピボットは左上固定
		_glyphDesc.curvature = a_desc.curvature;
		_glyphDesc.pClipRect = a_desc.pClipRect;
		_glyphDesc.postEffectFlags = a_desc.postEffectFlags;
		_glyphDesc.effect = a_desc.effect;

		for (size_t _lineIndex = 0; _lineIndex < _lines.size(); ++_lineIndex)
		{
			const auto& _line = _lines[_lineIndex];

			// 行揃え : ブロック幅に対して行を寄せる
			float _lineStart = 0.0f;
			switch (a_desc.textAlign)
			{
			case ETextAlign::Center: _lineStart = (_blockWidth - _lineWidthVec[_lineIndex]) * 0.5f; break;
			case ETextAlign::Right:  _lineStart = (_blockWidth - _lineWidthVec[_lineIndex]);        break;
			case ETextAlign::Left:
			default: break;
			}

			// ベースラインは行の上端から ascent ぶん下
			const float _baselineY = _lineHeight * static_cast<float>(_lineIndex) + _ascent;

			float _penX = _lineStart;
			uint32_t _prev = 0;

			for (const uint32_t _codePoint : _line)
			{
				const Resource::Glyph* _pGlyph = _pFont->RequestGlyph(_codePoint);
				if (_pGlyph == nullptr) continue;

				if (_prev != 0) _penX += _pFont->GetKerning(_prev, _codePoint);

				if (!_pGlyph->IsEmpty())
				{
					// フォント基準サイズでの位置を出してから、まとめて縮小する
					const Math::Vector2 _localTopLeft = {
						_blockTopLeft.x + (_penX + _pGlyph->xOffset) * _fontScale,
						_blockTopLeft.y + (_baselineY + _pGlyph->yOffset) * _fontScale
					};
					const Math::Vector2 _glyphSize = {
						static_cast<float>(_pGlyph->width) * _fontScale,
						static_cast<float>(_pGlyph->height) * _fontScale
					};

					_glyphDesc.pixelPos = a_desc.pixelPos + RotateDeg(_localTopLeft, a_desc.rotationDeg);
					_glyphDesc.pixelSize = _glyphSize;
					_glyphDesc.uvOffset = {
						static_cast<float>(_pGlyph->x) / _atlasSize,
						static_cast<float>(_pGlyph->y) / _atlasSize
					};
					_glyphDesc.uvScale = {
						static_cast<float>(_pGlyph->width) / _atlasSize,
						static_cast<float>(_pGlyph->height) / _atlasSize
					};

					// この文字の中心が、弧の頂点からどれだけずれているか(回転前の px)
					_glyphDesc.curveOffsetX = a_desc.curveOffsetX + _localTopLeft.x + _glyphSize.x * 0.5f;
					_glyphDesc.curveOffsetY = a_desc.curveOffsetY + _localTopLeft.y + _glyphSize.y * 0.5f;

					SubmitUI(_glyphDesc);
				}

				_penX += _pGlyph->xAdvance + _charSpacingInFontUnit;
				_prev = _codePoint;
			}
		}
	}

	//------------------------------------------------------------------------------------------
	// アニメーションするモデルの BLAS と頂点領域を用意する
	//
	// 要求はワールドごとに積まれる(AnimationModelStartSystem など)ので、ワールドを受け取る。
	// 以前は SceneManager の一番上のワールドだけを見ていたため、
	// 下に重なったシーンやエフェクトエディターのワールドの要求を取りこぼしていた。
	//
	// コマンドは専用のリストに積んで先に提出する。
	// 提出した順に流れるので、同じフレームの Execute(スキニング・BLAS更新)より前に構築される
	//------------------------------------------------------------------------------------------
	void DrawSubmitter::ProcessDynamicRaytracingInit(ECS::World& a_world)
	{
		// 必須リソースの存在チェック
		if (!a_world.HasResource<Pool::ItemPool<Raytracing::DynamicRaytracingData>>()) return;
		if (!a_world.HasResource<std::vector<Engine::Graphics::Raytracing::DynamicRaytracingInitRequest>>()) return;

		auto& _initRequestVec = a_world.RefResource<std::vector<Engine::Graphics::Raytracing::DynamicRaytracingInitRequest>>();
		if (_initRequestVec.empty()) return;

		auto& _dynamicPool = a_world.RefResource<Pool::ItemPool<Raytracing::DynamicRaytracingData>>();

		auto* _pDevice = m_pRenderDevice->RefDevice();
		auto* _pCmdList = m_pRenderDevice->AcquireDirectCommandList();

		// モデルのリソースからBLASと頂点バッファをコピー
		for (auto& _initReq : _initRequestVec)
		{
			// ターゲットとなるインスタンスデータと、ソースとなるモデルデータの取得
			auto* _pData = _dynamicPool.Ref(_initReq.dynamicInstanceHandle);
			auto* _pModel = (*m_pResourceManager).Get(_initReq.modelHandle);
			if (!_pData || !_pModel) continue;

			// モデル内の各メッシュごとに動的BLASを構築
			for (auto& _meshHandle : _pModel->GetMeshHandles())
			{
				// メッシュの有効性チェック
				auto* _pMesh = (*m_pResourceManager).Get(_meshHandle);
				if (!_pMesh || !_pMesh->HasRtData()) continue;

				// メッシュデータの追加と参照の取得
				_pData->meshDataVec.emplace_back();
				auto& _targetMeshData = _pData->meshDataVec.back();

				// インスタンス専用のアニメーション用頂点バッファ領域をメガバッファから割り当て
				UINT _vertexCount = _pMesh->GetRtData().vertexHandle.count;
				_targetMeshData.animatedVertexHandle = m_pMeshBufferAllocator->AllocateAnimatedVertex(_vertexCount);

				// サブメッシュ（マテリアル単位）ごとのジオメトリ情報を構築
				std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> _descVec = {};
				_descVec.reserve(_pMesh->GetMetaData().subsets.size());

				// レイトレーシング用データ作成
				for (auto& _subset : _pMesh->GetMetaData().subsets)
				{
					// ジオメトリ記述作成
					D3D12_RAYTRACING_GEOMETRY_DESC _desc = {};
					_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
					_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
					// 頂点バッファ
					_desc.Triangles.VertexBuffer.StartAddress =
						m_pMeshBufferAllocator->GetAnimatedVertexBuffer().GetGPUVirtualAddress() +
						(_targetMeshData.animatedVertexHandle.startIndex * sizeof(Resource::AnimatedVertex));
					_desc.Triangles.VertexBuffer.StrideInBytes = sizeof(Resource::AnimatedVertex);	// 先頭の座標だけをこの間隔で読む
					_desc.Triangles.VertexCount = _vertexCount;
					_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

					// インデックスバッファ
					_desc.Triangles.IndexBuffer =
						m_pMeshBufferAllocator->GetIndexBuffer().GetGPUVirtualAddress() +
						sizeof(uint32_t) * (_subset.faceStart * 3 + _pMesh->GetRtData().indexHandle.startIndex);
					_desc.Triangles.IndexCount = _subset.faceCount * 3;
					_desc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;

					_descVec.push_back(_desc);
				}
				_pData->meshDataVec.back().instanceBLAS.CreateDynamic(
					_pDevice,
					_pCmdList,
					_descVec
				);
				_pData->meshDataVec.back().meshHandle = _meshHandle;
			}
		}

		m_pRenderDevice->SubmitDirectCommandList(_pCmdList);

		// 処理が終われば命令を解放
		_initRequestVec.clear();
	}
	//------------------------------------------------------------------------------------------
	// テクスチャのSRV番号を引く。引けなければ -1
	//
	// シェーダーは負の番号を「テクスチャ無し」として扱う(MeshGBufferPS など)。
	// 次のどれでも -1 を返し、落とさずに素の値で描かせる。
	//   ・テクスチャを持たないマテリアル(法線マップ無しなど)
	//   ・非同期ロード中 : スロットには空の実体が入っていて、SRVはまだ無い
	//   ・読み込み失敗
	// 以前は見つからないと null を参照していた(ログを切ったビルドでは ERRLOG が消えるので素通りする)
	//------------------------------------------------------------------------------------------
	int DrawSubmitter::GetSRVIndexFromTextureHandle(const Handle<Resource::Texture>& a_texHandle) const
	{
		auto& _resManager = (*m_pResourceManager);
		if (!_resManager.IsReady(a_texHandle)) return -1;

		const auto* _pTex = _resManager.Get(a_texHandle);
		if (!_pTex) return -1;

		const auto& _srv = _pTex->GetSRV();
		if (!_srv.IsValid()) return -1;

		return static_cast<int>(_srv.GetIndex());
	}

	// シェーディングモデルはもう引かない。
	// 以前はここで解決できないと描画コマンドごと捨てていたので、
	// シェーディングモデルを持たないマテリアルは何も描かれなかった
	bool DrawSubmitter::FetchDrawResources(
		const Resource::ModelDrawCommand& a_cmd,
		const Resource::Mesh*& a_pOutMesh,
		const Resource::Material*& a_pOutMaterial)
	{
		auto& _resManager = (*m_pResourceManager);

		// メッシュ
		a_pOutMesh = _resManager.Get(a_cmd.meshHandle);
		if (!a_pOutMesh) return false;

		// マテリアル
		a_pOutMaterial = _resManager.Get(a_cmd.materialHandle);
		if (!a_pOutMaterial) return false;

		return true;
	}

	MeshMaterial DrawSubmitter::BuildMeshMaterial(
		const Resource::Material* a_pMaterial,
		const Math::Color& a_albedoScale,
		const Math::Vector3& a_emissiveScale,
		const Math::Vector3& a_emissiveAdd)
	{
		MeshMaterial _meshMaterial = {};
		_meshMaterial.baseColor = a_pMaterial->baseColor * a_albedoScale;
		_meshMaterial.emissive = a_pMaterial->emissive * a_emissiveScale;
		_meshMaterial.emissiveAdd = a_emissiveAdd;
		_meshMaterial.metallic = a_pMaterial->metallic;
		_meshMaterial.roughness = a_pMaterial->roughness;
		_meshMaterial.albedoIndex = GetSRVIndexFromTextureHandle(a_pMaterial->baseColorTex);
		_meshMaterial.metaRoughnessIndex = GetSRVIndexFromTextureHandle(a_pMaterial->metaRoughTex);
		_meshMaterial.emissiveIndex = GetSRVIndexFromTextureHandle(a_pMaterial->emissiveTex);
		_meshMaterial.normalIndex = GetSRVIndexFromTextureHandle(a_pMaterial->normalTex);
		return _meshMaterial;
	}

	void DrawSubmitter::RegisterDrawCommandToPasses(
		const Resource::ModelDrawCommand& a_cmd,
		const Resource::Mesh* a_pMesh,
		const Resource::Material* a_pMaterial,
		const Math::Matrix& a_mat,
		const Math::Matrix& a_prevMat,
		bool a_isAnimation,
		uint32_t a_animatedVertexStart,
		const Math::Color& a_albedoScale,
		const Math::Vector3& a_emissiveScale,
		const Math::Vector3& a_emissiveAdd,
		PSOKey a_psoKey,
		uint32_t a_lodStart,
		uint32_t a_lodCount,
		float a_fade)
	{
		// マテリアルの透明モードで、どちらのキューへ流すかを決める。
		// Mask はアルファで抜くだけで前後関係は不透明と同じ扱いなので Opaque
		const EGeometryQueue _queue = (a_pMaterial->alphaMode == Resource::EAlpha::Blend)
			? EGeometryQueue::Transparent
			: EGeometryQueue::Opaque;
		RegisterDrawCommandToPasses(
			_queue, a_cmd, a_pMesh, a_pMaterial, 
			a_mat, a_prevMat, a_isAnimation, a_animatedVertexStart, 
			a_albedoScale, a_emissiveScale, a_emissiveAdd, a_psoKey,
			a_lodStart, a_lodCount, a_fade
		);
	}

	void DrawSubmitter::RegisterDrawCommandToPasses(const EGeometryQueue& a_eGeoQue, const Resource::ModelDrawCommand& a_cmd, const Resource::Mesh* a_pMesh, const Resource::Material* a_pMaterial, const Math::Matrix& a_mat, const Math::Matrix& a_prevMat, bool a_isAnimation, uint32_t a_animatedVertexStart, const Math::Color& a_albedoScale, const Math::Vector3& a_emissiveScale, const Math::Vector3& a_emissiveAdd, PSOKey a_psoKey, uint32_t a_lodStart, uint32_t a_lodCount, float a_fade)
	{
		const EGeometryQueue _queue = a_eGeoQue;

		//------------------------------------------------------------------------------------------
		// フェード(カメラに近い障害物・自機など)
		//
		// 値はインスタンスデータに載せ、GBuffer のピクセルシェーダーがディザーで画素を捨てる。
		// GBuffer は ZPre の深度と EQUAL で比べるので、ZPre でも同じ画素を捨てないと
		// 捨てた画素に自分の深度が残り、後ろの物まで描かれなくなる。
		// 深度だけのパスはふだんピクセルシェーダーを持たないので、フェード中だけ
		// 別の PSO(DitherFade)にして、ディザー用のピクセルシェーダーを張らせる。
		// 完全に見えているもの(1)は今までと同じ PSO のまま
		//------------------------------------------------------------------------------------------
		const float _fade = std::clamp(a_fade, 0.0f, 1.0f);
		if (_fade < 1.0f)
		{
			a_psoKey.permutationFlags |= static_cast<uint32_t>(EShaderPermutationFlags::DitherFade);
		}

		const auto& _msData = a_pMesh->GetMeshShaderData();
		const auto& _subsetMeshlet = _msData.subsetMeshlets[a_cmd.subIdx];
		const bool _isTransparent = (_queue == EGeometryQueue::Transparent);

		//------------------------------------------------------------------------------------------
		// マテリアルとインスタンスのデータは、パスをまたいで1つを共有する
		//
		// どちらもパスに依存する値を持たない(ワールド行列・メッシュレットの位置・マテリアル値だけ)。
		// パスごとに作ると、同じ中身がパスの数 × カメラの数だけGPUバッファへ積まれ、
		// テクスチャのSRV番号も同じ回数だけ引き直すことになる。
		// 作るのは最初にアイテムを積めたパスのときだけ(PSOが無くて1つも積めなければ作らない)
		//------------------------------------------------------------------------------------------
		constexpr UINT NOT_CREATED = UINT_MAX;
		UINT _meshInstanceIndex = NOT_CREATED;
		auto _acquireInstanceIndex = [&]() -> UINT
			{
				if (_meshInstanceIndex != NOT_CREATED) return _meshInstanceIndex;

				const MeshMaterial _meshMaterial = BuildMeshMaterial(a_pMaterial, a_albedoScale, a_emissiveScale, a_emissiveAdd);

				MeshInstanceData _meshInstanceData = {};
				_meshInstanceData.worldMat = a_mat.Transpose();
				_meshInstanceData.prevWorldMat = a_prevMat.Transpose();
				_meshInstanceData.materialOffset = m_pDrawLists->AddMeshMaterial(_meshMaterial);
				_meshInstanceData.meshletOffset = _msData.meshletHandle.startIndex + _subsetMeshlet.meshletOffset;
				_meshInstanceData.vertexOffset = a_pMesh->GetRtData().vertexHandle.startIndex;
				_meshInstanceData.uviOffset = _msData.uniqueVertexIndicesHandle.startIndex;
				_meshInstanceData.primitiveOffset = _msData.primitiveIndicesHandle.startIndex;
				_meshInstanceData.cullStart = _msData.cullDataHandle.startIndex + _subsetMeshlet.cullOffset;
				_meshInstanceData.meshletCount = _subsetMeshlet.meshletCount;
				_meshInstanceData.animatedVertexStart = a_animatedVertexStart;
				_meshInstanceData.isAnimated = a_isAnimation ? 1 : 0;
				_meshInstanceData.lodStart = a_lodStart;
				_meshInstanceData.lodCount = a_lodCount;
				_meshInstanceData.fade = _fade;

				_meshInstanceIndex = m_pDrawLists->AddInstanceData(_meshInstanceData);
				return _meshInstanceIndex;
			};

		// モデルを受け取るパスへアイテムを流す。
		// パスごとにPSOもパス番号も違うので、アイテム自体はパスの数だけ積む
		for (auto* _pPipelinePass : m_pCameraPipelines->GetGeometryPasses(_queue))
		{
			if (!_pPipelinePass) continue;

			PSOKey _pipelineKey = a_psoKey;
			_pipelineKey.permutationFlags |= static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::MeshShader);
			_pipelineKey.psHandle = _pPipelinePass->GetDefaultPSHandle();

			auto _psoHandle = _pPipelinePass->RefPipelineBuilder().Request(_pipelineKey, m_pPipelineStateManager, *m_pResourceManager);

			// PSOを作れなかったアイテムは積まない : 描くときに引く先が無い。
			//
			// 番号そのものはハンドルと同じ16bitをソートキーに持たせてあるので、
			// もう「収まらない」ことは起きない。弾くのは無効ハンドルだけ。
			// ここは1フレームに何万回も通るので警告は出さない。
			// 理由(シェーダーがまだ読めていない等)は PipelineStateManager が
			// PSOごとに1回だけ知らせている
			if (!_psoHandle.IsValid()) continue;

			Engine::Graphics::LightWeightDrawItem _item = {};
			_item.meshHandle = a_cmd.meshHandle;
			_item.materialHandle = a_cmd.materialHandle;
			_item.isAnimation = a_isAnimation;
			_item.subIndex = a_cmd.subIdx;
			_item.meshInstanceIndex = _acquireInstanceIndex();
			_item.subsetMeshletCount = _subsetMeshlet.meshletCount;
			_item.psoID = _psoHandle.GetIndex();

			// ソートキー。
			// 半透明は奥から手前へ描かないと重なりが崩れるので、深さで並べる。
			// 深さはカメラが確定してから(SortItems の直前に)決めるので、ここでは位置だけ控える
			if (_isTransparent)
			{
				_item.isTransparent = true;
				_item.sortPos = { a_mat._41, a_mat._42, a_mat._43 };
				_item.sortKey.transparentBits.psoID = _psoHandle.GetIndex();
				_item.sortKey.transparentBits.passIndex = _pPipelinePass->GetPassIndex();
			}
			else
			{
				_item.sortKey.bits.subIndex = a_cmd.subIdx;
				_item.sortKey.bits.meshID = a_cmd.meshHandle.GetIndex();
				_item.sortKey.bits.materialID = a_cmd.materialHandle.GetIndex();
				_item.sortKey.bits.psoID = _psoHandle.GetIndex();
				_item.sortKey.bits.passIndex = _pPipelinePass->GetPassIndex();
			}

			m_pDrawLists->AddItem(_item);
		}
	}

	void DrawSubmitter::PushMeshLodData(
		const Resource::Model& a_model,
		size_t a_cmdIndex,
		const Resource::ModelDrawCommand& a_baseCmd,
		const Resource::Mesh* a_pBaseMesh,
		const Math::Vector4& a_worldSphere,
		uint32_t& a_outStart,
		uint32_t& a_outCount)
	{
		a_outStart = 0;
		a_outCount = 0;

		const auto& _lodModels = a_model.GetLodModels();
		const auto& _lodLevels = a_model.GetLodLevelVec();
		const size_t _levelCount = (std::min)({ _lodModels.size(), _lodLevels.size(), Resource::MAX_MODEL_LOD_LEVELS });
		if (_levelCount == 0 || !a_pBaseMesh) return;

		// メッシュの位置をそのまま段へ写す(インスタンスデータと同じ求め方)
		auto _fillFromMesh = [](MeshLodData& a_out, const Resource::Mesh& a_mesh, const Resource::SubsetMeshletData& a_subset)
			{
				const auto& _msData = a_mesh.GetMeshShaderData();
				a_out.meshletOffset = _msData.meshletHandle.startIndex + a_subset.meshletOffset;
				a_out.meshletCount = a_subset.meshletCount;
				a_out.cullStart = _msData.cullDataHandle.startIndex + a_subset.cullOffset;
				a_out.vertexOffset = a_mesh.GetRtData().vertexHandle.startIndex;
				a_out.uviOffset = _msData.uniqueVertexIndicesHandle.startIndex;
				a_out.primitiveOffset = _msData.primitiveIndicesHandle.startIndex;
			};

		const auto& _baseSubset = a_pBaseMesh->GetMeshShaderData().subsetMeshlets[a_baseCmd.subIdx];

		// 増幅シェーダーは LOD0 のメッシュレット数でグループを起こす(1グループ32枚)。
		// それより多いメッシュレットを持つ段は描き切れないので、その段も LOD0 で描く
		const uint32_t _maxMeshletCount = ((_baseSubset.meshletCount + 31u) / 32u) * 32u;

		std::array<MeshLodData, Resource::MAX_MODEL_LOD_LEVELS> _levels = {};
		for (size_t _k = 0; _k < _levelCount; ++_k)
		{
			MeshLodData& _level = _levels[_k];
			_level.boundingSphere = a_worldSphere;
			_level.screenSize = _lodLevels[_k].screenSize;

			// 既定は LOD0 と同じもの : 段のモデルがまだ読めていない・描き切れないとき
			_fillFromMesh(_level, *a_pBaseMesh, _baseSubset);

			const Resource::Model* _pLodModel = m_pResourceManager->Get(_lodModels[_k]);
			if (!_pLodModel) continue;

			// LOD0 と同じ番号の描画コマンドが、この段の代わりのサブセット。
			// 無ければこの段ではこのサブセットを描かない(粗い段でまとめて消した部品など)
			const auto& _lodCmdVec = _pLodModel->GetDrawCommandVec();
			if (a_cmdIndex >= _lodCmdVec.size())
			{
				_level.meshletCount = 0;
				continue;
			}

			const auto& _lodCmd = _lodCmdVec[a_cmdIndex];
			const Resource::Mesh* _pLodMesh = m_pResourceManager->Get(_lodCmd.meshHandle);
			if (!_pLodMesh) continue;

			const auto& _lodSubsetVec = _pLodMesh->GetMeshShaderData().subsetMeshlets;
			if (_lodCmd.subIdx >= _lodSubsetVec.size()) continue;

			const auto& _lodSubset = _lodSubsetVec[_lodCmd.subIdx];
			if (_lodSubset.meshletCount > _maxMeshletCount) continue;

			_fillFromMesh(_level, *_pLodMesh, _lodSubset);
		}

		// 表に収まらなければ LOD0 だけで描く
		UINT _start = 0;
		if (!m_pDrawLists->AddMeshLodData(std::span<const MeshLodData>(_levels.data(), _levelCount), _start)) return;

		a_outStart = _start;
		a_outCount = static_cast<uint32_t>(_levelCount);
	}

	void DrawSubmitter::PushUIData(uint32_t a_texIndex, uint32_t a_noiseTexIndex, const SpriteSubmitDesc& a_desc)
	{
		// スクリーン解像度(px)
		const float _w = static_cast<float>(m_renderWidth);
		const float _h = static_cast<float>(m_renderHeight);
		if (_w <= 0.0f || _h <= 0.0f) return;

		// 回転(度→ラジアン)。回転はピクセル空間(等方)で行い、そのあとNDCへ変換する。
		// こうしないと、NDC空間(x,yで縮尺が違う)で回転させたときに斜めで画像が歪む。
		const float _rad = DirectX::XMConvertToRadians(a_desc.rotationDeg);
		const float _cos = std::cos(_rad);
		const float _sin = std::sin(_rad);

		// 半サイズ(px)と、ピボット(正規化[0,1])からクアッド中心までのオフセット(px)。
		// (0.5 - pivot) * size がクアッド中心のピボットからのずれ。
		const Math::Vector2 _halfPx = { a_desc.pixelSize.x * 0.5f, a_desc.pixelSize.y * 0.5f };
		const Math::Vector2 _pivotOffPx = {
			(0.5f - a_desc.pivot.x) * a_desc.pixelSize.x,
			(0.5f - a_desc.pivot.y) * a_desc.pixelSize.y
		};

		// クアッド頂点 q∈[-1,1] に対し、ピクセル空間での最終座標は
		//   finalPx = centerPx + q.x*axisXpx + q.y*axisYpx
		// 回転はピボットを中心に行うので、centerPx = ピボット位置 + R*ピボットオフセット。
		//
		// ベースクアッドのUVは q.x=+1 がテクスチャ右、q.y=+1 がテクスチャ上(v=0)。
		// よって未回転時、ローカル+Xは画面右(+pixelX)、ローカル+Yは画面上(-pixelY)を向く。
		// この基底(+X=右, +Y=上)を回転行列 R(θ) で回す。
		//   axisXpx = R*( halfX,      0) = ( halfX*cos, halfX*sin)
		//   axisYpx = R*(     0, -halfY) = ( halfY*sin,-halfY*cos)
		const Math::Vector2 _centerPx = {
			a_desc.pixelPos.x + (_pivotOffPx.x * _cos - _pivotOffPx.y * _sin),
			a_desc.pixelPos.y + (_pivotOffPx.x * _sin + _pivotOffPx.y * _cos)
		};
		const Math::Vector2 _axisXpx = { _halfPx.x * _cos,  _halfPx.x * _sin };
		const Math::Vector2 _axisYpx = { _halfPx.y * _sin, -_halfPx.y * _cos };

		// ピクセル(左上原点/Y下向き) → NDC(中心原点/Y上向き)。
		// 点は原点シフトあり、方向ベクトルはスケールのみ(Yは符号反転)。
		UIData _data = {};
		_data.pos   = { _centerPx.x / _w * 2.0f - 1.0f, 1.0f - _centerPx.y / _h * 2.0f };
		_data.axisX = { _axisXpx.x * 2.0f / _w, -_axisXpx.y * 2.0f / _h };
		_data.axisY = { _axisYpx.x * 2.0f / _w, -_axisYpx.y * 2.0f / _h };
		_data.uvOffset = a_desc.uvOffset;
		_data.uvScale = a_desc.uvScale;
		_data.color = Math::DX::ToVector4(a_desc.color);
		_data.layer = a_desc.Zlayer;
		_data.texIndex = a_texIndex;
		// 湾曲。
		// 曲げは「弧の頂点からのずれ(px)」で決まるので、シェーダーが px へ戻せるように
		// このクアッドの実寸(半分の大きさ)も一緒に送る。
		// NDCの基底(axisX/axisY)からは画面解像度なしにpxを復元できないため
		_data.curvature = a_desc.curvature;
		_data.curveOffsetX = a_desc.curveOffsetX;
		_data.curveOffsetY = a_desc.curveOffsetY;
		_data.curveHalfWidth = _halfPx.x;
		_data.curveHalfHeight = _halfPx.y;

		// 切り抜き。点と同じくピクセル → NDC へ直す。
		// Y は上下が入れ替わるので、ピクセルの下端(max.y)が NDC の下端(minY)になる
		if (a_desc.pClipRect)
		{
			_data.clipRect = {
				a_desc.pClipRect->min.x / _w * 2.0f - 1.0f,
				1.0f - a_desc.pClipRect->max.y / _h * 2.0f,
				a_desc.pClipRect->max.x / _w * 2.0f - 1.0f,
				1.0f - a_desc.pClipRect->min.y / _h * 2.0f
			};
		}

		_data.postEffectFlags = a_desc.postEffectFlags;

		//--------------------------------------------------------------
		// 効果(UIマテリアル)
		//
		// ノイズの座標は NDC で渡す。原点は点なので原点シフトあり、
		// 1px あたりの量は方向なので Y の向きだけ反転する(ピクセルは下向き、NDC は上向き)。
		//   noiseUV = (ndc - originNdc) * noiseUVPerNdc
		//           = (px - originPx) / noisePixel
		//--------------------------------------------------------------
		const UIEffectDesc& _effect = a_desc.effect;
		_data.effectType = _effect.effect;
		if (_effect.effect != static_cast<uint32_t>(EUIEffect::None))
		{
			_data.noiseTexIndex = a_noiseTexIndex;
			_data.effectProgress = _effect.progress;
			_data.effectOriginNdc = {
				_effect.originPixel.x / _w * 2.0f - 1.0f,
				1.0f - _effect.originPixel.y / _h * 2.0f
			};

			const float _noisePixel = std::max(_effect.noisePixel, 1.0f);
			_data.noiseUVPerNdc = {
				 (_w * 0.5f) / _noisePixel,
				-(_h * 0.5f) / _noisePixel
			};

			_data.edgeColor = Math::DX::ToVector4(_effect.edgeColor);
			_data.edgeWidth = _effect.edgeWidth;
		}

		m_pDrawLists->AddUI(_data);
	}
}
