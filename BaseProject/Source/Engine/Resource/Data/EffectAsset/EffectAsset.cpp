#include "EffectAsset.h"

#include "../../Manager/ResourceManager/ResourceManager.h"

namespace Engine::Resource
{
	const char* ToString(EEffectSpace a_space)
	{
		switch (a_space)
		{
		case EEffectSpace::LocalOffset:		return "LocalOffset";
		case EEffectSpace::WorldMatrix:		return "WorldMatrix";
		case EEffectSpace::ReverseVelocity:	return "ReverseVelocity";
		default:							return "Unknown";
		}
	}

	const char* ToString(EEffectParamTarget a_target)
	{
		switch (a_target)
		{
		case EEffectParamTarget::ParticleEmitCount:	return "ParticleEmitCount";
		case EEffectParamTarget::ParticleSpeed:		return "ParticleSpeed";
		case EEffectParamTarget::ParticleSize:		return "ParticleSize";
		case EEffectParamTarget::MeshScale:			return "MeshScale";
		case EEffectParamTarget::MeshEmissive:		return "MeshEmissive";
		case EEffectParamTarget::SoundVolume:		return "SoundVolume";
		default:									return "Unknown";
		}
	}

	//======================================================================================
	// EffectLightPart
	//======================================================================================
	void EffectLightPart::Archive(Persistence::Archive& a_ar)
	{
		a_ar.Field("PosOffset", posOffset);
		a_ar.Field("Color", color);
		a_ar.Field("Brightness", brightness);
		a_ar.Field("Range", range);

		timing.Archive(a_ar);

		a_ar.Field("EndBrightness", endBrightness);
		a_ar.Field("Trigger", timing.trigger);
	}

	const char* ToString(EEffectTrailBlend a_blend)
	{
		switch (a_blend)
		{
		case EEffectTrailBlend::AlphaBlend:	return "AlphaBlend";
		case EEffectTrailBlend::Additive:	return "Additive";
		default:							return "Unknown";
		}
	}

	//======================================================================================
	// EffectTrailPart
	//======================================================================================
	void EffectTrailPart::Archive(Persistence::Archive& a_ar)
	{
		a_ar.Field("PosOffset", posOffset);

		timing.Archive(a_ar);
		a_ar.Field("Trigger", timing.trigger);

		a_ar.Field("LifeTime", lifeTime);
		a_ar.Field("MaxPoints", maxPoints);
		a_ar.Field("MinVertexDistance", minVertexDistance);

		a_ar.Field("Width", width);
		a_ar.Field("EndWidth", endWidth);
		a_ar.Field("HeadColor", headColor);
		a_ar.Field("TailColor", tailColor);
		a_ar.Field("Intensity", intensity);
		a_ar.Field("Blend", blend);
		a_ar.Field("TextureGUID", textureGUID);

		// ※ 追加は末尾に。バイナリは順次読みなので途中に挿すと既存データが全部ずれる
	}

	//======================================================================================
	// EffectParamBinding
	//======================================================================================
	void EffectParamBinding::Archive(Persistence::Archive& a_ar)
	{
		a_ar.Field("ParamIndex", paramIndex);
		a_ar.Field("Target", target);
		a_ar.Field("PartIndex", partIndex);
		a_ar.Field("ScaleAtZero", scaleAtZero);
		a_ar.Field("ScaleAtOne", scaleAtOne);
	}

	const char* ToString(EEffectSimulationSpace a_space)
	{
		switch (a_space)
		{
		case EEffectSimulationSpace::Inherit:	return "Inherit";
		case EEffectSimulationSpace::World:		return "World";
		case EEffectSimulationSpace::Local:		return "Local";
		default:								return "Unknown";
		}
	}

	const char* ToString(EEffectTrigger a_trigger)
	{
		switch (a_trigger)
		{
		case EEffectTrigger::OnPlay:	return "OnPlay";
		case EEffectTrigger::OnStop:	return "OnStop";
		default:						return "Unknown";
		}
	}

	//======================================================================================
	// EffectTiming
	//======================================================================================
	void EffectTiming::Archive(Persistence::Archive& a_ar)
	{
		a_ar.Field("StartDelay", startDelay);
		a_ar.Field("Duration", duration);
	}

	//======================================================================================
	// EffectParticlePart
	//======================================================================================
	void EffectParticlePart::Archive(Persistence::Archive& a_ar)
	{
		a_ar.Field("ParticleGUID", particleGUID);

		a_ar.Field("Space", space);
		a_ar.Field("PosOffset", posOffset);
		a_ar.Field("EmitDir", emitDir);

		a_ar.Field("EmitCount", emitCount);
		a_ar.Field("EmitRate", emitRate);

		timing.Archive(a_ar);

		a_ar.Field("BaseScale", baseScale);
		a_ar.Field("MinScale", minScale);
		a_ar.Field("MaxScale", maxScale);
		a_ar.Field("PositionRadius", positionRadius);
		a_ar.Field("DirectionAngle", directionAngle);

		// ※ 追加は末尾に。バイナリは順次読みなので途中に挿すと既存データが全部ずれる
		a_ar.Field("EmitShape", emitShape);
		a_ar.Field("Trigger", timing.trigger);
		a_ar.Field("SimulationSpace", simulationSpace);
		a_ar.Field("PositionExtents", positionExtents);
	}

	bool EffectParticlePart::IsLocalSimulation(const ParticlesAsset* a_pParticle) const
	{
		switch (simulationSpace)
		{
		case EEffectSimulationSpace::World:	return false;
		case EEffectSimulationSpace::Local:	return true;
		case EEffectSimulationSpace::Inherit:
		default:
			return a_pParticle && a_pParticle->IsLocalSpace();
		}
	}

	//======================================================================================
	// EffectMeshPart
	//======================================================================================
	void EffectMeshPart::Archive(Persistence::Archive& a_ar)
	{
		a_ar.Field("ModelGUID", modelGUID);

		a_ar.Field("PosOffset", posOffset);
		a_ar.Field("Rotation", rotation);
		a_ar.Field("Scale", scale);

		timing.Archive(a_ar);

		a_ar.Field("ColorScale", colorScale);
		a_ar.Field("EmissiveColor", emissiveColor);
		a_ar.Field("EmissiveIntensity", emissiveIntensity);

		a_ar.Field("EndScale", endScale);
		a_ar.Field("EndAlpha", endAlpha);
		a_ar.Field("EndEmissiveIntensity", endEmissiveIntensity);

		// ※ 追加は末尾に
		a_ar.Field("Trigger", timing.trigger);
	}

	//======================================================================================
	// EffectSoundPart
	//======================================================================================
	void EffectSoundPart::Archive(Persistence::Archive& a_ar)
	{
		a_ar.Field("SoundGUID", soundGUID);

		timing.Archive(a_ar);

		a_ar.Field("Vol", vol);
		a_ar.Field("IsLoop", isLoop);
		a_ar.Field("Is3DSound", is3DSound);
		a_ar.Field("DistanceScaler", distanceScaler);
		a_ar.Field("IsWaitFinish", isWaitFinish);

		// ※ 追加は末尾に
		a_ar.Field("Trigger", timing.trigger);
		a_ar.Field("MinInterval", minInterval);
		a_ar.Field("MaxConcurrent", maxConcurrent);
	}

	//======================================================================================
	// EffectAsset : 編集
	//======================================================================================
	bool EffectAsset::AddParticlePart()
	{
		if (m_particleParts.size() >= EFFECT_PARTICLE_MAX) return false;
		m_particleParts.emplace_back();
		return true;
	}

	bool EffectAsset::AddMeshPart()
	{
		if (m_meshParts.size() >= EFFECT_MESH_MAX) return false;
		m_meshParts.emplace_back();
		return true;
	}

	bool EffectAsset::AddSoundPart()
	{
		if (m_soundParts.size() >= EFFECT_SOUND_MAX) return false;
		m_soundParts.emplace_back();
		return true;
	}

	void EffectAsset::RemoveParticlePart(size_t a_index)
	{
		if (a_index >= m_particleParts.size()) return;
		m_particleParts.erase(m_particleParts.begin() + a_index);
	}

	void EffectAsset::RemoveMeshPart(size_t a_index)
	{
		if (a_index >= m_meshParts.size()) return;
		m_meshParts.erase(m_meshParts.begin() + a_index);
	}

	void EffectAsset::RemoveSoundPart(size_t a_index)
	{
		if (a_index >= m_soundParts.size()) return;
		m_soundParts.erase(m_soundParts.begin() + a_index);
	}

	bool EffectAsset::AddLightPart()
	{
		if (m_lightParts.size() >= EFFECT_POINTLIGHT_MAX) return false;
		m_lightParts.emplace_back();
		return true;
	}

	void EffectAsset::RemoveLightPart(size_t a_index)
	{
		if (a_index >= m_lightParts.size()) return;
		m_lightParts.erase(m_lightParts.begin() + a_index);
	}

	bool EffectAsset::AddTrailPart()
	{
		if (m_trailParts.size() >= EFFECT_TRAIL_MAX) return false;
		m_trailParts.emplace_back();
		return true;
	}

	void EffectAsset::RemoveTrailPart(size_t a_index)
	{
		if (a_index >= m_trailParts.size()) return;
		m_trailParts.erase(m_trailParts.begin() + a_index);
	}

	bool EffectAsset::AddParamBinding()
	{
		if (m_paramBindings.size() >= EFFECT_PARAM_BINDING_MAX) return false;
		m_paramBindings.emplace_back();
		return true;
	}

	void EffectAsset::RemoveParamBinding(size_t a_index)
	{
		if (a_index >= m_paramBindings.size()) return;
		m_paramBindings.erase(m_paramBindings.begin() + a_index);
	}

	float EffectAsset::EvaluateParamScale(EEffectParamTarget a_target, size_t a_partIndex, const float* a_pParams) const
	{
		float _scale = 1.0f;
		for (const auto& _binding : m_paramBindings)
		{
			if (_binding.target != a_target) continue;
			if (_binding.partIndex >= 0 && static_cast<size_t>(_binding.partIndex) != a_partIndex) continue;
			if (_binding.paramIndex < 0 || static_cast<size_t>(_binding.paramIndex) >= EFFECT_PARAM_MAX) continue;

			const float _value = a_pParams ? a_pParams[_binding.paramIndex] : 0.0f;

			// 同じ先へ複数結び付けたら掛け合わせる
			_scale *= _binding.Evaluate(_value);
		}
		return _scale;
	}

	bool EffectAsset::HasParamBinding(EEffectParamTarget a_target) const
	{
		for (const auto& _binding : m_paramBindings)
		{
			if (_binding.target == a_target) return true;
		}
		return false;
	}

	//======================================================================================
	// EffectAsset : 保存・読み込み
	//======================================================================================
	void EffectAsset::Archive(Persistence::Archive& a_ar)
	{
		a_ar.StringField("Name", m_name);

		//------------------------------------------------------------------
		// パーティクル
		//
		// 個数から書くので、パーツを増やしても既存のファイルは読める
		// (中身の並びを変えた場合はバイナリが崩れるので作り直しが要る)
		//------------------------------------------------------------------
		size_t _particleCount = m_particleParts.size();
		if (a_ar.BeginArray("ParticleParts", _particleCount))
		{
			// ファイルに入っている数はそのまま読み切る。
			// 上限超過をここで打ち切るとバイナリの読み位置がずれて、
			// 後ろに続くメッシュパーツまで壊れる
			if (a_ar.IsLoading())
			{
				m_particleParts.assign(_particleCount, EffectParticlePart{});
			}

			for (size_t _i = 0; _i < _particleCount; ++_i)
			{
				if (a_ar.BeginObject(_i))
				{
					m_particleParts[_i].Archive(a_ar);
					a_ar.EndObject();
				}
			}
			a_ar.EndArray();

			// 読み切ってから切り捨てる。
			// 実体側(EffectInstance)の進行状態が固定長なので、上限を超えたぶんは動かせない
			if (a_ar.IsLoading() && m_particleParts.size() > EFFECT_PARTICLE_MAX)
			{
				m_particleParts.resize(EFFECT_PARTICLE_MAX);
			}
		}

		//------------------------------------------------------------------
		// メッシュ
		//------------------------------------------------------------------
		size_t _meshCount = m_meshParts.size();
		if (a_ar.BeginArray("MeshParts", _meshCount))
		{
			if (a_ar.IsLoading())
			{
				m_meshParts.assign(_meshCount, EffectMeshPart{});
			}

			for (size_t _i = 0; _i < _meshCount; ++_i)
			{
				if (a_ar.BeginObject(_i))
				{
					m_meshParts[_i].Archive(a_ar);
					a_ar.EndObject();
				}
			}
			a_ar.EndArray();

			if (a_ar.IsLoading() && m_meshParts.size() > EFFECT_MESH_MAX)
			{
				m_meshParts.resize(EFFECT_MESH_MAX);
			}
		}

		//------------------------------------------------------------------
		// サウンド
		//
		// 後から足した並びなので、必ず末尾に置くこと。
		// JSON は「SoundParts が無ければ音なし」で素通しできるが、
		// バイナリはキーを持たない順次読みなので、途中に挿すと
		// 保存済みの .obeffect が全部ずれる
		//------------------------------------------------------------------
		size_t _soundCount = m_soundParts.size();
		if (a_ar.BeginArray("SoundParts", _soundCount))
		{
			if (a_ar.IsLoading())
			{
				m_soundParts.assign(_soundCount, EffectSoundPart{});
			}

			for (size_t _i = 0; _i < _soundCount; ++_i)
			{
				if (a_ar.BeginObject(_i))
				{
					m_soundParts[_i].Archive(a_ar);
					a_ar.EndObject();
				}
			}
			a_ar.EndArray();

			// 実体側(EffectInstance)の声の席が固定長なので、超えたぶんは鳴らせない
			if (a_ar.IsLoading() && m_soundParts.size() > EFFECT_SOUND_MAX)
			{
				m_soundParts.resize(EFFECT_SOUND_MAX);
			}
		}

		//------------------------------------------------------------------
		// 個体ごとのパラメータの結び付け
		//
		// 後から足した並びなので、必ず末尾に置くこと(サウンドと同じ理由)
		//------------------------------------------------------------------
		size_t _bindingCount = m_paramBindings.size();
		if (a_ar.BeginArray("ParamBindings", _bindingCount))
		{
			if (a_ar.IsLoading())
			{
				m_paramBindings.assign(_bindingCount, EffectParamBinding{});
			}

			for (size_t _i = 0; _i < _bindingCount; ++_i)
			{
				if (a_ar.BeginObject(_i))
				{
					m_paramBindings[_i].Archive(a_ar);
					a_ar.EndObject();
				}
			}
			a_ar.EndArray();

			if (a_ar.IsLoading() && m_paramBindings.size() > EFFECT_PARAM_BINDING_MAX)
			{
				m_paramBindings.resize(EFFECT_PARAM_BINDING_MAX);
			}
		}

		//------------------------------------------------------------------
		// ライト
		//
		// 後から足した並びなので、必ず末尾に置くこと(サウンドと同じ理由)
		//------------------------------------------------------------------
		size_t _lightCount = m_lightParts.size();
		if (a_ar.BeginArray("LightParts", _lightCount))
		{
			if (a_ar.IsLoading())
			{
				m_lightParts.assign(_lightCount, EffectLightPart{});
			}

			for (size_t _i = 0; _i < _lightCount; ++_i)
			{
				if (a_ar.BeginObject(_i))
				{
					m_lightParts[_i].Archive(a_ar);
					a_ar.EndObject();
				}
			}
			a_ar.EndArray();

			// 実体側(EffectInstance)のライトの席が固定長なので、超えたぶんは出せない
			if (a_ar.IsLoading() && m_lightParts.size() > EFFECT_POINTLIGHT_MAX)
			{
				m_lightParts.resize(EFFECT_POINTLIGHT_MAX);
			}
		}

		//------------------------------------------------------------------
		// トレイル
		//
		// 後から足した並びなので、必ず末尾に置くこと(サウンドと同じ理由)
		//------------------------------------------------------------------
		size_t _trailCount = m_trailParts.size();
		if (a_ar.BeginArray("TrailParts", _trailCount))
		{
			if (a_ar.IsLoading())
			{
				m_trailParts.assign(_trailCount, EffectTrailPart{});
			}

			for (size_t _i = 0; _i < _trailCount; ++_i)
			{
				if (a_ar.BeginObject(_i))
				{
					m_trailParts[_i].Archive(a_ar);
					a_ar.EndObject();
				}
			}
			a_ar.EndArray();

			// 実体側(EffectInstance)のトレイルの席が固定長なので、超えたぶんは引けない
			if (a_ar.IsLoading() && m_trailParts.size() > EFFECT_TRAIL_MAX)
			{
				m_trailParts.resize(EFFECT_TRAIL_MAX);
			}
		}
	}

	void EffectAsset::Save(const std::string& a_baseFilePath)
	{
		auto _fileDir = Core::File::GetDirFromPath(a_baseFilePath);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_baseFilePath);

		Persistence::Archive _ar(Persistence::Archive::EMode::Save, _fileDir, _fileName, "effect");
		Archive(_ar);
	}

	bool EffectAsset::HasStopParts() const
	{
		for (const auto& _part : m_particleParts) { if (_part.IsValid() && _part.timing.IsStopTrigger()) return true; }
		for (const auto& _part : m_meshParts)     { if (_part.IsValid() && _part.timing.IsStopTrigger()) return true; }
		for (const auto& _part : m_soundParts)    { if (_part.IsValid() && _part.timing.IsStopTrigger()) return true; }
		for (const auto& _part : m_lightParts)    { if (_part.IsValid() && _part.timing.IsStopTrigger()) return true; }
		for (const auto& _part : m_trailParts)    { if (_part.IsValid() && _part.timing.IsStopTrigger()) return true; }
		return false;
	}

	void EffectAsset::ResolveReferences(ResourceManager& a_resourceManager)
	{
		auto& _resourceManager = a_resourceManager;

		for (auto& _part : m_particleParts)
		{
			if (!_part.IsValid())
			{
				_part.particleHandle = {};
				continue;
			}
			_part.particleHandle = _resourceManager.LoadImmediate<ParticlesAsset>(_part.particleGUID);
		}

		for (auto& _part : m_meshParts)
		{
			if (!_part.IsValid())
			{
				_part.modelHandle = {};
				continue;
			}
			_part.modelHandle = _resourceManager.LoadImmediate<Model>(_part.modelGUID);
		}

		// 波形はここで読んでおく。
		// 実際に鳴らすのは声(SoundInstance)の方で、そちらは使う側が1つずつ持つが、
		// 元の波形は全員で共有できる。持っておかないと
		// 初めて鳴らすエフェクトが湧いた瞬間に読み込みが走る
		for (auto& _part : m_soundParts)
		{
			if (!_part.IsValid())
			{
				_part.soundHandle = {};
				continue;
			}
			_part.soundHandle = _resourceManager.LoadImmediate<Sound>(_part.soundGUID);
		}

		// トレイルに貼る絵
		for (auto& _part : m_trailParts)
		{
			if (_part.textureGUID == Core::DEFAULT_GUID)
			{
				_part.textureHandle = {};
				continue;
			}
			_part.textureHandle = _resourceManager.LoadImmediate<Texture>(_part.textureGUID);
		}
	}

}
