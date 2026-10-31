#include "EffectAssetEdit.h"

#include "../../../../../Helper/EditorHelper.h"

#include "../../AssetLink.h"
#include "../../../../../EffectEditor/EffectEditor.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

namespace Editor::Inspector
{
	namespace
	{
		//-----------------------------------------------------------------------------------------
		// 割り当てたアセットを出す。GUIDだけでは何を指しているか分からないため
		//
		// アセットインスペクターから来ているときはリンクになり、押すと中身へ飛べる。
		// エフェクトエディターから来ているときは飛び先が無いので名前のまま
		//-----------------------------------------------------------------------------------------
		void DrawAssignedName(EditorContext* a_pEditContext, const Core::GUID& a_guid)
		{
			if (a_guid == Core::DEFAULT_GUID)
			{
				// 空欄はエラーではないことを明示しておく
				Engine::EditorField::HelpText("(empty : このパーツは出ない)");
				return;
			}

			DrawAssetLink(a_pEditContext, "", a_guid);
		}

		//-----------------------------------------------------------------------------------------
		// 時間指定 : いつ出て、どれだけ続くか
		//-----------------------------------------------------------------------------------------
		// いつ動き出すか(再生したとき / 止めたとき)
		bool TriggerEdit(Resource::EffectTiming& a_timing)
		{
			const bool _isChanged = Engine::EditorField::Field("Trigger", a_timing.trigger);
			Engine::EditorField::Tooltip(a_timing.IsStopTrigger()
				? "止めたときに動く(消火の火花・終了音など)。StartDelay / Duration は止めてからの時間"
				: "再生したときに動く。StartDelay / Duration は再生してからの時間");
			return _isChanged;
		}

		bool TimingEdit(Resource::EffectTiming& a_timing)
		{
			bool _isChanged = false;

			if (TriggerEdit(a_timing)) _isChanged = true;
			if (Engine::EditorField::Field("StartDelay (s)", a_timing.startDelay, 0.01f, 0.0f)) _isChanged = true;
			if (Engine::EditorField::Field("Duration (s, 0=infinite)", a_timing.duration, 0.01f, 0.0f)) _isChanged = true;

			if (a_timing.duration <= 0.0f)
			{
				Engine::EditorField::HelpText(a_timing.IsStopTrigger()
					? "一度きり(止めたときにバーストで出す)"
					: "止めるまで出し続ける");
			}

			return _isChanged;
		}

		//-----------------------------------------------------------------------------------------
		// パーティクル1件
		//-----------------------------------------------------------------------------------------
		bool ParticlePartEdit(const ECS::EngineServices& a_services, EditorContext* a_pEditContext, Resource::EffectParticlePart& a_part)
		{
			bool _isChanged = false;

			// ---- 何を出すか ----
			if (EditorField::AssetField(a_services, "Particle", "ParticlesAsset", a_part.particleGUID))
			{
				_isChanged = true;
			}
			DrawAssignedName(a_pEditContext, a_part.particleGUID);

			if (a_part.IsValid())
			{
				Engine::EditorField::SameLine();
				if (EditorField::DeleteButton("Clear"))
				{
					a_part.particleGUID = Core::DEFAULT_GUID;
					_isChanged = true;
				}
			}

			Engine::EditorField::Header("Emit Source");

			// ---- どこから出すか ----
			EditorField::Field("Space", a_part.space);
			if (a_part.space == Resource::EEffectSpace::LocalOffset)
			{
				Engine::EditorField::Field("PosOffset", a_part.posOffset, 0.05f);
				Engine::EditorField::Field("EmitDir (local)", a_part.emitDir, 0.05f);
			}
			else if (a_part.space == Resource::EEffectSpace::ReverseVelocity)
			{
				Engine::EditorField::Field("PosOffset", a_part.posOffset, 0.05f);
				Engine::EditorField::Tooltip("Dir : -Velocity (fallback : -Forward)");
			}
			else
			{
				Engine::EditorField::HelpText("Pos/Dir : 付いている相手の行列そのまま");
			}
			Engine::EditorField::Tooltip("出す瞬間の位置と向きの決め方。持ち主のスケールは掛からない");

			// ---- 出したあと、どの座標系で回すか ----
			Engine::EditorField::Header("Simulation");
			if (EditorField::Field("SimulationSpace", a_part.simulationSpace)) _isChanged = true;
			{
				// 読み込み済みのものだけ引く(未読込なら World 扱いの表示になる)
				const Resource::ParticlesAsset* _pParticle = nullptr;
				if (a_part.IsValid() && a_services.pResourceManager)
				{
					const auto _handle = a_services.pResourceManager->GetCache<Resource::ParticlesAsset>(a_part.particleGUID);
					_pParticle = a_services.pResourceManager->Get(_handle);
				}

				const bool _isLocal = a_part.IsLocalSimulation(_pParticle);
				if (a_part.simulationSpace == Resource::EEffectSimulationSpace::Inherit)
				{
					Engine::EditorField::HelpText(_pParticle
						? (_isLocal ? "パーティクルアセットの設定 : Local" : "パーティクルアセットの設定 : World")
						: "(パーティクルが読めていないので World 扱い)");
				}
				Engine::EditorField::HelpText(_isLocal
					? "発生源にくっついて動く(ブースターの噴射など)"
					: "出したその場に残る(煙・爆発・弾の軌跡など)");
			}

			Engine::EditorField::Header("Emit Shape");

			// ---- どっちへ出すか ----
			EditorField::Field("Shape", a_part.emitShape);
			switch (a_part.emitShape)
			{
			case Graphics::Particle::EParticleEmitShape::Sphere:
				Engine::EditorField::HelpText("中心から全方向へ均等に飛び散る(爆発向き)");
				break;
			case Graphics::Particle::EParticleEmitShape::Hemisphere:
				Engine::EditorField::HelpText("EmitDir 側の半球だけへ飛び散る(地面での爆発向き)");
				break;
			case Graphics::Particle::EParticleEmitShape::Cone:
			default:
				Engine::EditorField::HelpText("EmitDir を軸にした円錐。広がりは DirectionAngle");
				break;
			}

			Engine::EditorField::Header("Emission");

			// ---- どれだけ出すか ----
			Engine::EditorField::Field("EmitCount", a_part.emitCount, 1, 0);
			Engine::EditorField::Field("EmitRate (/s, 0=Burst)", a_part.emitRate, 0.5f, 0.0f);
			if (a_part.emitRate <= 0.0f)
			{
				Engine::EditorField::HelpText("出し始めに一度だけ EmitCount 個");
			}

			Engine::EditorField::Header("Timing");
			TimingEdit(a_part.timing);

			Engine::EditorField::Header("Shape");

			// ---- 散らばり方 ----
			// 1粒の速度・寿命はパーティクルアセット側なので、ここには出さない
			Engine::EditorField::Field("BaseScale", a_part.baseScale, 0.05f, 0.0f);
			Engine::EditorField::Field("MinScale", a_part.minScale, 0.01f, 0.0f);
			Engine::EditorField::Field("MaxScale", a_part.maxScale, 0.01f, 0.0f);
			Engine::EditorField::Field("PositionRadius", a_part.positionRadius, 0.05f, 0.0f);
			Engine::EditorField::Field("PositionExtents", a_part.positionExtents, 0.05f, 0.0f);
			Engine::EditorField::Tooltip("発生位置を散らす箱の半分の大きさ(X = 横、Y = 上、Z = EmitDir)。0 で箱なし");

			// 円錐のときしか効かない値なので、それ以外では触らせない
			ImGui::BeginDisabled(a_part.emitShape != Graphics::Particle::EParticleEmitShape::Cone);
			Engine::EditorField::Field("DirectionAngle (deg)", a_part.directionAngle, 0.5f, 0.0f, 180.0f);
			ImGui::EndDisabled();
			if (a_part.emitShape != Graphics::Particle::EParticleEmitShape::Cone)
			{
				Engine::EditorField::HelpText("(DirectionAngle は Cone のときだけ効きます)");
			}

			Engine::EditorField::HelpText("初速・寿命・絵・減衰・色はパーティクルアセット側");

			return _isChanged;
		}

		//-----------------------------------------------------------------------------------------
		// メッシュ1件
		//-----------------------------------------------------------------------------------------
		bool MeshPartEdit(const ECS::EngineServices& a_services, EditorContext* a_pEditContext, Resource::EffectMeshPart& a_part)
		{
			bool _isChanged = false;

			// ---- 何を出すか ----
			if (EditorField::AssetField(a_services, "Model", "Model", a_part.modelGUID))
			{
				_isChanged = true;
			}
			DrawAssignedName(a_pEditContext, a_part.modelGUID);

			if (a_part.IsValid())
			{
				Engine::EditorField::SameLine();
				if (EditorField::DeleteButton("Clear"))
				{
					a_part.modelGUID = Core::DEFAULT_GUID;
					_isChanged = true;
				}
			}

			Engine::EditorField::Header("Transform");
			Engine::EditorField::HelpText("付いている相手の行列基準のローカル配置");
			Engine::EditorField::Field("PosOffset", a_part.posOffset, 0.05f);
			Engine::EditorField::Field("Rotation (deg)", a_part.rotation, 1.0f);
			Engine::EditorField::Field("Scale", a_part.scale, 0.05f);

			Engine::EditorField::Header("Timing");
			TimingEdit(a_part.timing);

			Engine::EditorField::Header("Look");
			Engine::EditorField::ColorField("ColorScale", a_part.colorScale);
			Engine::EditorField::ColorField("EmissiveColor", a_part.emissiveColor);
			Engine::EditorField::Field("EmissiveIntensity", a_part.emissiveIntensity, 0.05f, 0.0f);
			Engine::EditorField::Tooltip("ブルームのしきい値(既定1.0)を超えると光る");

			Engine::EditorField::Header("End (Duration の終わりでの値)");
			if (a_part.timing.duration <= 0.0f)
			{
				// duration が無いと補間する区間が無い
				Engine::EditorField::HelpText("Duration が 0 の間は変化しない");
			}
			Engine::EditorField::Field("EndScale (倍率)", a_part.endScale, 0.05f, 0.0f);
			Engine::EditorField::Field("EndAlpha", a_part.endAlpha, 0.01f, 0.0f, 1.0f);
			Engine::EditorField::Field("EndEmissiveIntensity", a_part.endEmissiveIntensity, 0.05f, 0.0f);

			return _isChanged;
		}

		//-----------------------------------------------------------------------------------------
		// サウンド1件
		//-----------------------------------------------------------------------------------------
		bool SoundPartEdit(const ECS::EngineServices& a_services, EditorContext* a_pEditContext, Resource::EffectSoundPart& a_part)
		{
			bool _isChanged = false;

			// ---- 何を鳴らすか ----
			if (EditorField::AssetField(a_services, "Sound", "Sound", a_part.soundGUID))
			{
				_isChanged = true;
			}
			DrawAssignedName(a_pEditContext, a_part.soundGUID);

			if (a_part.IsValid())
			{
				Engine::EditorField::SameLine();
				if (EditorField::DeleteButton("Clear"))
				{
					a_part.soundGUID = Core::DEFAULT_GUID;
					_isChanged = true;
				}
			}

			Engine::EditorField::Header("Timing");
			TriggerEdit(a_part.timing);
			Engine::EditorField::HelpText(a_part.timing.IsStopTrigger()
				? "StartDelay : 止めてから何秒後に鳴らすか"
				: "StartDelay : 再生から何秒後に鳴らすか");
			Engine::EditorField::Field("StartDelay (s)", a_part.timing.startDelay, 0.01f, 0.0f);

			// Duration はループ音を止めるための長さ。単発音では使わない
			ImGui::BeginDisabled(!a_part.isLoop);
			Engine::EditorField::Field("Duration (s, 0=infinite)", a_part.timing.duration, 0.01f, 0.0f);
			ImGui::EndDisabled();
			if (!a_part.isLoop)
			{
				Engine::EditorField::HelpText("(Duration は Loop のときだけ効きます)");
			}
			else if (a_part.timing.duration <= 0.0f)
			{
				Engine::EditorField::HelpText("エフェクトを止めるまで鳴らし続ける");
			}

			Engine::EditorField::Header("Play");
			Engine::EditorField::Field("Volume", a_part.vol, 0.01f, 0.0f, 1.0f);
			Engine::EditorField::Field("Loop", a_part.isLoop);
			Engine::EditorField::Tooltip(a_part.isLoop
				? "鳴りっぱなし。エフェクトを止めると一緒に止まる"
				: "一度だけ鳴らす。エフェクトを止めても鳴りきる");

			Engine::EditorField::Field("3D Sound", a_part.is3DSound);
			Engine::EditorField::Tooltip(a_part.is3DSound
				? "エフェクトの居場所で鳴る(定位・距離減衰あり)"
				: "常に同じ音量で鳴る(UI・全体演出向き)");

			ImGui::BeginDisabled(!a_part.is3DSound);
			Engine::EditorField::Field("DistanceScaler", a_part.distanceScaler, 0.05f, 0.0f);
			ImGui::EndDisabled();
			if (a_part.is3DSound)
			{
				Engine::EditorField::HelpText("大きいほど遠くまで届く(1 = 通常)");
			}

			Engine::EditorField::Header("Finish");
			Engine::EditorField::Field("WaitFinish", a_part.isWaitFinish);
			Engine::EditorField::Tooltip(a_part.isWaitFinish
				? "この音が鳴り終わるまでエフェクトを終わらせない"
				: "音の長さを見ない(絵が終わればエフェクトも終わる)");
			Engine::EditorField::HelpText("DestroyOnFinish のエフェクトで音が途切れるのを防ぐ設定");

			// 同じ音を鳴らしすぎない(エフェクトをまたいで、同じ音ごとに数える)
			Engine::EditorField::Header("Limit");
			Engine::EditorField::Field("MinInterval (s)", a_part.minInterval, 0.01f, 0.0f);
			Engine::EditorField::Tooltip("前回この音を鳴らしてから、この秒数が経つまでは鳴らさない(0 で制限なし)。被弾音の連打を間引く");
			Engine::EditorField::Field("MaxConcurrent", a_part.maxConcurrent, 1.0f, 0);
			Engine::EditorField::Tooltip("この音が同時に鳴っている数の上限(0 で制限なし)。間引かれた音は鳴らさない");

			return _isChanged;
		}

		//-----------------------------------------------------------------------------------------
		// ライト1件
		//-----------------------------------------------------------------------------------------
		bool LightPartEdit(Resource::EffectLightPart& a_part)
		{
			bool _isChanged = false;

			Engine::EditorField::Header("Place");
			if (Engine::EditorField::Field("PosOffset", a_part.posOffset, 0.05f)) _isChanged = true;
			Engine::EditorField::Tooltip("エフェクトの置き場(上書きの位置・向き・大きさ込み)を基準にした位置");

			Engine::EditorField::Header("Light");
			if (Engine::EditorField::ColorField("Color", a_part.color)) _isChanged = true;
			if (Engine::EditorField::Field("Brightness", a_part.brightness, 0.05f, 0.0f)) _isChanged = true;
			if (Engine::EditorField::Field("Range (m)", a_part.range, 0.05f, 0.0f)) _isChanged = true;
			Engine::EditorField::Tooltip("光の届く距離。エフェクト全体の大きさ倍率も掛かる。0 でこのパーツは出ない");

			Engine::EditorField::Header("Timing");
			if (TimingEdit(a_part.timing)) _isChanged = true;

			Engine::EditorField::Header("End (Duration の終わりでの値)");
			if (a_part.timing.duration <= 0.0f)
			{
				Engine::EditorField::HelpText("Duration が 0 の間は変化しない");
			}
			if (Engine::EditorField::Field("EndBrightness", a_part.endBrightness, 0.05f, 0.0f)) _isChanged = true;
			Engine::EditorField::Tooltip("閃光なら 0 へ落とす");

			return _isChanged;
		}

		//-----------------------------------------------------------------------------------------
		// トレイル1件
		//-----------------------------------------------------------------------------------------
		bool TrailPartEdit(const ECS::EngineServices& a_services, EditorContext* a_pEditContext, Resource::EffectTrailPart& a_part)
		{
			bool _isChanged = false;

			Engine::EditorField::Header("Place");
			Engine::EditorField::Field("PosOffset", a_part.posOffset, 0.05f);
			Engine::EditorField::Tooltip("エフェクトの置き場(上書きの位置・向き・大きさ込み)を基準にした、軌跡を引く点");

			Engine::EditorField::Header("Timing");
			TimingEdit(a_part.timing);
			Engine::EditorField::HelpText("引く時間帯を外れても、残った帯は LifeTime をかけて尾から消える");

			Engine::EditorField::Header("Length");
			Engine::EditorField::Field("LifeTime (s)", a_part.lifeTime, 0.01f, 0.0f);
			Engine::EditorField::Tooltip("1つの点が残る秒数。長いほど帯が長く残る。0 でこのパーツは出ない");
			Engine::EditorField::Field("MaxPoints", a_part.maxPoints, 1.0f, 2, Resource::EFFECT_TRAIL_POINT_MAX);
			Engine::EditorField::Tooltip("点の数の上限。超えたら古い点から捨てる");
			Engine::EditorField::Field("MinVertexDistance (m)", a_part.minVertexDistance, 0.01f, 0.0f);
			Engine::EditorField::Tooltip("前の点からこの距離動くまでは新しい点を足さない(先頭だけ動かす)。エフェクトの大きさ倍率も掛かる");

			Engine::EditorField::Header("Look");
			Engine::EditorField::Field("Width (m)", a_part.width, 0.01f, 0.0f);
			Engine::EditorField::Field("EndWidth (m)", a_part.endWidth, 0.01f, 0.0f);
			Engine::EditorField::Tooltip("先頭の幅から尾の幅へ、点の年齢で変わる。エフェクトの大きさ倍率も掛かる");
			Engine::EditorField::ColorField("HeadColor", a_part.headColor);
			Engine::EditorField::ColorField("TailColor", a_part.tailColor);
			Engine::EditorField::Tooltip("a は濃さ。尾の a を 0 にすると、ふわっと消える");
			Engine::EditorField::Field("Intensity", a_part.intensity, 0.05f, 0.0f);
			Engine::EditorField::Tooltip("色に掛ける強さ。ブルームのしきい値(既定1.0)を超えると光る");
			Engine::EditorField::Field("Blend", a_part.blend);
			Engine::EditorField::HelpText(a_part.blend == Resource::EEffectTrailBlend::Additive
				? "下地へ足す(光の軌跡・剣筋)"
				: "下地を濃さのぶん隠して色を置く(煙の筋)");

			// ---- 帯に貼る絵 ----
			if (EditorField::AssetField(a_services, "Texture", "Texture", a_part.textureGUID))
			{
				_isChanged = true;
			}
			if (a_part.textureGUID == Core::DEFAULT_GUID)
			{
				Engine::EditorField::HelpText("(empty : 色だけで描く)");
			}
			else
			{
				DrawAssetLink(a_pEditContext, "", a_part.textureGUID);
				Engine::EditorField::SameLine();
				if (EditorField::DeleteButton("Clear"))
				{
					a_part.textureGUID = Core::DEFAULT_GUID;
					_isChanged = true;
				}
			}
			Engine::EditorField::HelpText("U = 先頭 → 尾、V = 帯の端 → 端");

			Engine::EditorField::HelpText("描くのは半透明パス(TransparentPass)。パイプラインに置かれていないと出ない");

			return _isChanged;
		}
	}

	//-----------------------------------------------------------------------------------------
	// エフェクトアセットの編集・詳細表示
	//-----------------------------------------------------------------------------------------
	void EffectAssetEdit(
		const ECS::EngineServices& a_services,
		const Core::GUID& a_guid,
		Resource::EffectAsset* a_pEffect,
		bool a_isShowOpenEditorButton,
		EditorContext* a_pEditContext)
	{
		if (!a_pEffect) { return; }

		const auto& _guid = a_guid;

		Engine::EditorField::Value("Effect", "%s", a_pEffect->GetName().c_str());
		Engine::EditorField::Line();

		// 保存ボタン
		if (ImGui::Button("Save Asset") && a_services.pAssetDatabase)
		{
			auto _filePath = a_services.pAssetDatabase->GetFilePathFromGUID(_guid);
			a_pEffect->Save(_filePath);
			ENGINE_LOG("Save EffectAsset : %s", _filePath.c_str());
		}

		// 単体確認用の画面を開く。
		// 開いている間はゲームのシーンが止まり、このエフェクトだけがゲームと同じ
		// レンダーグラフで描かれる。ここでの編集はそのまま向こうの見た目に反映される
		if (a_isShowOpenEditorButton)
		{
			Engine::EditorField::SameLine();
			if (ImGui::Button("Open Effect Editor"))
			{
				if (auto* _pEffectEditor = a_pEditContext ? a_pEditContext->pEffectEditor : nullptr)
				{
					_pEffectEditor->Open(_guid);
				}
			}
		}

		// 参照アセットを引き直す必要があるか
		bool _isChanged = false;

		//------------------------------------------------------------------
		// パーティクルパーツ
		//------------------------------------------------------------------
		auto& _particleParts = a_pEffect->RefParticleParts();

		Engine::EditorField::Header("Particle Parts");
		Engine::EditorField::Text("%d / %d", static_cast<int>(_particleParts.size()), static_cast<int>(Resource::EFFECT_PARTICLE_MAX));

		// 上限まで来たら足せない(実体側の進行状態が固定長のため)
		ImGui::BeginDisabled(_particleParts.size() >= Resource::EFFECT_PARTICLE_MAX);
		if (EditorField::CreateButton("Add Particle Part"))
		{
			a_pEffect->AddParticlePart();
		}
		ImGui::EndDisabled();

		// 反復中に消すと崩れるので削除は予約する
		int _removeParticleIndex = -1;

		for (size_t _i = 0; _i < _particleParts.size(); ++_i)
		{
			ImGui::PushID(static_cast<int>(_i));

			const std::string _label = "Particle " + std::to_string(_i);
			if (ImGui::TreeNodeEx(_label.c_str(), ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_Framed))
			{
				if (EditorField::DeleteButton("Remove Part"))
				{
					_removeParticleIndex = static_cast<int>(_i);
				}

				if (ParticlePartEdit(a_services, a_pEditContext, _particleParts[_i])) _isChanged = true;

				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		if (_removeParticleIndex >= 0)
		{
			a_pEffect->RemoveParticlePart(static_cast<size_t>(_removeParticleIndex));
		}

		//------------------------------------------------------------------
		// メッシュパーツ
		//------------------------------------------------------------------
		auto& _meshParts = a_pEffect->RefMeshParts();

		Engine::EditorField::Header("Mesh Parts");
		Engine::EditorField::Text("%d / %d", static_cast<int>(_meshParts.size()), static_cast<int>(Resource::EFFECT_MESH_MAX));

		ImGui::BeginDisabled(_meshParts.size() >= Resource::EFFECT_MESH_MAX);
		if (EditorField::CreateButton("Add Mesh Part"))
		{
			a_pEffect->AddMeshPart();
		}
		ImGui::EndDisabled();

		int _removeMeshIndex = -1;

		for (size_t _i = 0; _i < _meshParts.size(); ++_i)
		{
			// パーティクル側と番号が被るので、IDの土台をずらしておく
			ImGui::PushID(static_cast<int>(_i + Resource::EFFECT_PARTICLE_MAX));

			const std::string _label = "Mesh " + std::to_string(_i);
			if (ImGui::TreeNodeEx(_label.c_str(), ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_Framed))
			{
				if (EditorField::DeleteButton("Remove Part"))
				{
					_removeMeshIndex = static_cast<int>(_i);
				}

				if (MeshPartEdit(a_services, a_pEditContext, _meshParts[_i])) _isChanged = true;

				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		if (_removeMeshIndex >= 0)
		{
			a_pEffect->RemoveMeshPart(static_cast<size_t>(_removeMeshIndex));
		}

		//------------------------------------------------------------------
		// サウンドパーツ
		//
		// 絵と音がいつも一緒に出るものを1枚にまとめるための欄。
		// 出す側は再生を伝えるだけでよく、音を別に鳴らしに行かなくてよくなる
		//------------------------------------------------------------------
		auto& _soundParts = a_pEffect->RefSoundParts();

		Engine::EditorField::Header("Sound Parts");
		Engine::EditorField::Text("%d / %d", static_cast<int>(_soundParts.size()), static_cast<int>(Resource::EFFECT_SOUND_MAX));

		// 上限まで来たら足せない(実体側の声の席が固定長のため)
		ImGui::BeginDisabled(_soundParts.size() >= Resource::EFFECT_SOUND_MAX);
		if (EditorField::CreateButton("Add Sound Part"))
		{
			a_pEffect->AddSoundPart();
		}
		ImGui::EndDisabled();

		int _removeSoundIndex = -1;

		for (size_t _i = 0; _i < _soundParts.size(); ++_i)
		{
			// 上2つと番号が被らないように、IDの土台をずらしておく
			ImGui::PushID(static_cast<int>(_i + Resource::EFFECT_PARTICLE_MAX + Resource::EFFECT_MESH_MAX));

			const std::string _label = "Sound " + std::to_string(_i);
			if (ImGui::TreeNodeEx(_label.c_str(), ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_Framed))
			{
				if (EditorField::DeleteButton("Remove Part"))
				{
					_removeSoundIndex = static_cast<int>(_i);
				}

				if (SoundPartEdit(a_services, a_pEditContext, _soundParts[_i])) _isChanged = true;

				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		if (_removeSoundIndex >= 0)
		{
			a_pEffect->RemoveSoundPart(static_cast<size_t>(_removeSoundIndex));
		}

		//------------------------------------------------------------------
		// ライトパーツ
		//
		// 爆発の閃光・噴射の照り返しなど。出している間だけポイントライトを借りる
		//------------------------------------------------------------------
		auto& _lightParts = a_pEffect->RefLightParts();

		Engine::EditorField::Header("Light Parts");
		Engine::EditorField::Text("%d / %d", static_cast<int>(_lightParts.size()), static_cast<int>(Resource::EFFECT_POINTLIGHT_MAX));

		// 上限まで来たら足せない(実体側のライトの席が固定長のため)
		ImGui::BeginDisabled(_lightParts.size() >= Resource::EFFECT_POINTLIGHT_MAX);
		if (EditorField::CreateButton("Add Light Part"))
		{
			a_pEffect->AddLightPart();
		}
		ImGui::EndDisabled();

		int _removeLightIndex = -1;

		for (size_t _i = 0; _i < _lightParts.size(); ++_i)
		{
			// 上のパーツと番号が被らないように、IDの土台をずらしておく
			ImGui::PushID(static_cast<int>(_i + 1000));

			const std::string _label = "Light " + std::to_string(_i);
			if (ImGui::TreeNodeEx(_label.c_str(), ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_Framed))
			{
				if (EditorField::DeleteButton("Remove Part"))
				{
					_removeLightIndex = static_cast<int>(_i);
				}

				LightPartEdit(_lightParts[_i]);

				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		if (_removeLightIndex >= 0)
		{
			a_pEffect->RemoveLightPart(static_cast<size_t>(_removeLightIndex));
		}

		//------------------------------------------------------------------
		// トレイルパーツ
		//
		// 動いた跡に残る帯(剣筋・ミサイルの光跡など)。点の履歴は実行中に TrailNode が持つ
		//------------------------------------------------------------------
		auto& _trailParts = a_pEffect->RefTrailParts();

		Engine::EditorField::Header("Trail Parts");
		Engine::EditorField::Text("%d / %d", static_cast<int>(_trailParts.size()), static_cast<int>(Resource::EFFECT_TRAIL_MAX));

		// 上限まで来たら足せない(実体側のトレイルの席が固定長のため)
		ImGui::BeginDisabled(_trailParts.size() >= Resource::EFFECT_TRAIL_MAX);
		if (EditorField::CreateButton("Add Trail Part"))
		{
			a_pEffect->AddTrailPart();
		}
		ImGui::EndDisabled();

		int _removeTrailIndex = -1;

		for (size_t _i = 0; _i < _trailParts.size(); ++_i)
		{
			// 上のパーツと番号が被らないように、IDの土台をずらしておく
			ImGui::PushID(static_cast<int>(_i + 2000));

			const std::string _label = "Trail " + std::to_string(_i);
			if (ImGui::TreeNodeEx(_label.c_str(), ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_Framed))
			{
				if (EditorField::DeleteButton("Remove Part"))
				{
					_removeTrailIndex = static_cast<int>(_i);
				}

				if (TrailPartEdit(a_services, a_pEditContext, _trailParts[_i])) _isChanged = true;

				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		if (_removeTrailIndex >= 0)
		{
			a_pEffect->RemoveTrailPart(static_cast<size_t>(_removeTrailIndex));
		}

		//------------------------------------------------------------------
		// 個体ごとのパラメータの結び付け
		//
		// 出す側(EffectOverrideComponent::params)が書いた値を、どこにどれだけ効かせるか。
		// 倍率 = lerp(ScaleAtZero, ScaleAtOne, params[Param])。同じ先へ複数あれば掛け合わせる
		//------------------------------------------------------------------
		auto& _bindings = a_pEffect->RefParamBindings();

		Engine::EditorField::Header("Parameters");
		Engine::EditorField::HelpText("出す側が書いた値(0〜1)で、パーツの量や大きさを変える");
		Engine::EditorField::Text("%d / %d", static_cast<int>(_bindings.size()), static_cast<int>(Resource::EFFECT_PARAM_BINDING_MAX));

		ImGui::BeginDisabled(_bindings.size() >= Resource::EFFECT_PARAM_BINDING_MAX);
		if (EditorField::CreateButton("Add Binding"))
		{
			a_pEffect->AddParamBinding();
		}
		ImGui::EndDisabled();

		int _removeBindingIndex = -1;

		for (size_t _i = 0; _i < _bindings.size(); ++_i)
		{
			// 上のパーツと番号が被らないように、IDの土台をずらしておく
			ImGui::PushID(static_cast<int>(_i + Resource::EFFECT_PARTICLE_MAX + Resource::EFFECT_MESH_MAX + Resource::EFFECT_SOUND_MAX));

			Resource::EffectParamBinding& _binding = _bindings[_i];
			const std::string _label = "Binding " + std::to_string(_i) + " : Param " + std::to_string(_binding.paramIndex) + " -> " + Resource::ToString(_binding.target);
			if (ImGui::TreeNodeEx(_label.c_str(), ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_Framed))
			{
				if (EditorField::DeleteButton("Remove Binding"))
				{
					_removeBindingIndex = static_cast<int>(_i);
				}

				Engine::EditorField::Field("Param", _binding.paramIndex, 1.0f, 0, static_cast<int>(Resource::EFFECT_PARAM_MAX) - 1);
				Engine::EditorField::Tooltip("EffectOverrideComponent::params の何番を使うか");
				Engine::EditorField::Field("Target", _binding.target);
				Engine::EditorField::Field("PartIndex (-1=all)", _binding.partIndex, 1.0f, -1, 15);
				Engine::EditorField::Tooltip("効かせるパーツの番号。-1 でその種類のパーツ全部");
				Engine::EditorField::Field("ScaleAtZero", _binding.scaleAtZero, 0.01f);
				Engine::EditorField::Field("ScaleAtOne", _binding.scaleAtOne, 0.01f);

				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		if (_removeBindingIndex >= 0)
		{
			a_pEffect->RemoveParamBinding(static_cast<size_t>(_removeBindingIndex));
		}

		// 参照を差し替えたらハンドルを引き直す。
		// 保存前でもエディター上ですぐ確認できるようにしておく
		if (_isChanged)
		{
			a_pEffect->ResolveReferences(*a_services.pResourceManager);
		}
	}
}
