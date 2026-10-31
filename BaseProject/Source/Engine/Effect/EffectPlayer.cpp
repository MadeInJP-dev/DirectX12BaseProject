#include "EffectPlayer.h"

#include "Engine/Audio/AudioManager.h"
#include "Engine/Graphics/LightManager/Core/Light.h"

namespace Engine::Effect
{
	//======================================================================================
	// 声(サウンドインスタンス)
	//
	// 声は鳴らす直前に借り、鳴り終わったら返す(ループ音は止めたときに返す)。
	// 以前は生成時(Fixup)に借りてエンティティの寿命いっぱい握っていたので、
	// 爆発のような一発ものを大量に出すと、鳴る前から声が埋まっていた
	//======================================================================================
	namespace
	{
		// 借りている声を返す。何から借りたかも空にする
		void ReturnVoice(Engine::Audio::AudioManager& a_audioManager, EffectInstance& a_inst, size_t a_index)
		{
			a_audioManager.ReleaseSoundInstance(a_inst.soundHandles[a_index]);
			a_inst.soundHandles[a_index] = {};
			a_inst.soundSourceGUID[a_index] = Core::DEFAULT_GUID;
			a_inst.soundSource3D[a_index] = false;
		}

		// パーツの指定どおりの声を借りる(すでに借りていればそれを使う)
		Resource::SoundInstance* BorrowVoice(
			Engine::Audio::AudioManager& a_audioManager,
			EffectInstance& a_inst,
			size_t a_index,
			const Resource::EffectSoundPart& a_part)
		{
			if (auto* _pInstance = a_audioManager.RefInstance(a_inst.soundHandles[a_index]))
			{
				return _pInstance;
			}

			// 3D で鳴らすかは借りるときにしか決められない
			// (2Dで作ったインスタンスに Apply3D を掛けると DirectXTK が例外を投げる)
			a_inst.soundHandles[a_index] = a_audioManager.CreateSoundInstance(a_part.soundGUID, a_part.is3DSound);
			a_inst.soundSourceGUID[a_index] = a_part.soundGUID;
			a_inst.soundSource3D[a_index] = a_part.is3DSound;

			auto* _pInstance = a_audioManager.RefInstance(a_inst.soundHandles[a_index]);
			if (_pInstance)
			{
				_pInstance->SetVolume(a_part.vol);
				_pInstance->SetCurveDistanceScaler(a_part.distanceScaler);
				if (_pInstance->Is3D())
				{
					_pInstance->SetPos(a_inst.soundPos);
				}
			}
			return _pInstance;
		}
	}

	void EffectPlayer::PrepareSounds(const Resource::EffectAsset& a_asset, Engine::Audio::AudioManager& a_audioManager, EffectInstance& a_inst)
	{
		(void)a_asset;	// 声は鳴らす直前に借りるので、ここでは借りない

		// 作り直しでも漏らさないよう、持っているものを返して空にする
		a_inst.ReleaseSounds(a_audioManager);
	}

	void EffectPlayer::SyncSoundInstances(const Resource::EffectAsset& a_asset, Engine::Audio::AudioManager& a_audioManager, EffectInstance& a_inst)
	{
		const auto& _soundParts = a_asset.GetSoundParts();

		for (size_t _i = 0; _i < Resource::EFFECT_SOUND_MAX; ++_i)
		{
			// 借りていなければ合わせるものが無い
			if (!a_inst.soundHandles[_i].IsValid()) continue;

			//----------------------------------------------------------
			// このスロットに欲しい声
			//
			// パーツが無い/音が空のスロットは「声なし」が正しい姿。
			// エディターでパーツを消したり、音や 3D 指定を差し替えたときに、古い声が残らないようにする
			//----------------------------------------------------------
			Core::GUID _wantGUID = Core::DEFAULT_GUID;
			bool _want3D = false;
			if (_i < _soundParts.size() && _soundParts[_i].IsValid())
			{
				_wantGUID = _soundParts[_i].soundGUID;
				_want3D = _soundParts[_i].is3DSound;
			}

			// 食い違っていなければそのまま使う(ほとんどのフレームはこちら)
			if (a_inst.soundSourceGUID[_i] == _wantGUID &&
				a_inst.soundSource3D[_i] == _want3D)
			{
				continue;
			}

			// 古い声を返す。鳴らし直しは次に鳴らす番が来たときに、新しい指定で借りて行う
			ReturnVoice(a_audioManager, a_inst, _i);
			a_inst.soundTriggered[_i] = false;
		}
	}

	//======================================================================================
	// パーツの時間軸
	//
	// OnPlay のパーツは再生を始めてから(elapsed)、OnStop のパーツは止めてから(stopElapsed)の時間で動く。
	// 再生中は OnPlay だけ、止めている最中は OnStop だけが動く
	//======================================================================================
	namespace
	{
		// このパーツが今の状態で動く番か。動くなら、その時間軸での経過時間を返す
		bool IsPartTurn(const Resource::EffectTiming& a_timing, const EffectInstance& a_inst, float& a_outElapsed)
		{
			if (a_timing.IsStopTrigger())
			{
				a_outElapsed = a_inst.stopElapsed;
				return a_inst.isStopping;
			}
			a_outElapsed = a_inst.elapsed;
			return a_inst.isPlaying;
		}
	}

	void EffectPlayer::Play(const Resource::EffectAsset& a_asset, EffectInstance& a_inst)
	{
		(void)a_asset;	// いまは見ない

		// 止めている最中に再生し直されたら、止めたあとのパーツは打ち切って頭から。
		// (OnStop のループ音が鳴っていれば、次の Update で止める)
		a_inst.Reset();
		a_inst.isPlaying = true;
	}

	void EffectPlayer::Stop(const Resource::EffectAsset& a_asset, EffectInstance& a_inst, Engine::Audio::AudioManager* a_pAudioManager)
	{
		const auto& _soundParts = a_asset.GetSoundParts();

		a_inst.isPlaying = false;

		// 止めたフレームに出しかけていたぶんは捨てる
		for (size_t _i = 0; _i < Resource::EFFECT_PARTICLE_MAX; ++_i)
		{
			a_inst.pendingEmit[_i] = 0;
			a_inst.wasEmitting[_i] = false;
			a_inst.rateAccum[_i] = 0.0f;
		}

		//------------------------------------------------------------------
		// 止めたあとに動くパーツ(OnStop)があれば「止めている最中」へ。
		// 消火の火花や終了音は、ここから stopElapsed の時間で出る
		//------------------------------------------------------------------
		a_inst.isStopping = a_asset.HasStopParts();
		a_inst.stopElapsed = 0.0f;

		//------------------------------------------------------------------
		// 鳴っている音
		//
		// 止めるのはループを掛けたものだけ。噴射音のように鳴りっぱなしのものは
		// ここで止めないと吹かすのをやめても鳴り続ける。
		// 一方で単発音は鳴らしきらせる。点火の「ボッ」のような短い音まで切ると、
		// 出し入れの激しい演出で音がぶつ切りになるため
		//------------------------------------------------------------------
		if (a_pAudioManager)
		{
			const size_t _count = std::min<size_t>(_soundParts.size(), Resource::EFFECT_SOUND_MAX);
			for (size_t _i = 0; _i < _count; ++_i)
			{
				if (!_soundParts[_i].IsValid()) continue;

				// ループ音は止めて声を返す(単発音は鳴りきらせ、鳴り終わったら Update が返す)
				if (_soundParts[_i].isLoop)
				{
					ReturnVoice(*a_pAudioManager, a_inst, _i);
				}

				// 次に再生したときは鳴らし直す。OnStop の音はここから鳴らせるようになる
				a_inst.soundTriggered[_i] = false;
			}
		}
	}

	void EffectPlayer::Update(const Resource::EffectAsset& a_asset, EffectInstance& a_inst, float a_dt, Engine::Audio::AudioManager* a_pAudioManager, const float* a_pParams)
	{
		const auto& _particleParts = a_asset.GetParticleParts();
		const auto& _soundParts = a_asset.GetSoundParts();

		// 既定は今フレーム発生なし
		for (size_t _i = 0; _i < Resource::EFFECT_PARTICLE_MAX; ++_i)
		{
			a_inst.pendingEmit[_i] = 0;
		}

		//------------------------------------------------------------------
		// 鳴り終わった単発音の声を返す
		//
		// 止めた後(止まりきった後)でも単発音は鳴りきらせるので、ここは止まっていても回す。
		// 返しても「鳴らした」印は残すので、同じ再生の中で鳴り直すことはない
		//------------------------------------------------------------------
		if (a_pAudioManager)
		{
			for (size_t _i = 0; _i < Resource::EFFECT_SOUND_MAX; ++_i)
			{
				if (!a_inst.soundHandles[_i].IsValid()) continue;

				auto* _pInstance = a_pAudioManager->RefInstance(a_inst.soundHandles[_i]);
				if (!_pInstance)
				{
					a_inst.soundHandles[_i] = {};
					continue;
				}

				// ループ音は止めるまで返さない(止めるときに返す)
				const bool _isLoop = (_i < _soundParts.size()) && _soundParts[_i].isLoop;
				if (_isLoop && _pInstance->IsPlay()) continue;

				if (!_pInstance->IsPlay())
				{
					ReturnVoice(*a_pAudioManager, a_inst, _i);
				}
			}
		}

		if (!a_inst.IsActive()) return;

		// 時間を進める(再生中は再生してから、止めている最中は止めてからの時間)
		if (a_inst.isPlaying)
		{
			a_inst.elapsed += a_dt;
		}
		else
		{
			a_inst.stopElapsed += a_dt;
		}

		const size_t _count = std::min<size_t>(_particleParts.size(), Resource::EFFECT_PARTICLE_MAX);
		for (size_t _i = 0; _i < _count; ++_i)
		{
			const Resource::EffectParticlePart& _part = _particleParts[_i];

			// 中身が入っていないパーツは飛ばす
			if (!_part.IsValid()) continue;

			// 今の状態で動く番でない(再生中の OnStop / 止めている最中の OnPlay)、
			// または出す時間帯に入っていない(待ち時間中・終了済み)
			float _elapsed = 0.0f;
			if (!IsPartTurn(_part.timing, a_inst, _elapsed) || !_part.timing.IsActiveAt(_elapsed))
			{
				a_inst.rateAccum[_i] = 0.0f;
				a_inst.wasEmitting[_i] = false;
				continue;
			}

			if (_part.emitRate > 0.0f && !_part.timing.IsStopTrigger())
			{
				// ---- 連続発生 : 毎秒 emitRate 回 ----
				a_inst.rateAccum[_i] += a_dt;
				const float _interval = 1.0f / _part.emitRate;

				int _bursts = 0;
				// 溜まった分だけ発生させ、端数は残す。暴走防止に上限を設ける
				while (a_inst.rateAccum[_i] >= _interval && _bursts < 64)
				{
					a_inst.rateAccum[_i] -= _interval;
					++_bursts;
				}
				a_inst.pendingEmit[_i] = _bursts * _part.emitCount;
			}
			else if (_part.emitRate > 0.0f && _part.timing.duration > 0.0f)
			{
				// ---- 止めたあとの連続発生 : Duration のあいだだけ ----
				// (duration 0 の OnStop は一度きりなので、下のバーストとして扱う)
				a_inst.rateAccum[_i] += a_dt;
				const float _interval = 1.0f / _part.emitRate;

				int _bursts = 0;
				while (a_inst.rateAccum[_i] >= _interval && _bursts < 64)
				{
					a_inst.rateAccum[_i] -= _interval;
					++_bursts;
				}
				a_inst.pendingEmit[_i] = _bursts * _part.emitCount;
			}
			else
			{
				// ---- バースト : 出し始めのフレームで一度だけ ----
				if (!a_inst.wasEmitting[_i])
				{
					a_inst.pendingEmit[_i] = _part.emitCount;
				}
			}

			a_inst.wasEmitting[_i] = true;
		}

		//------------------------------------------------------------------
		// サウンド
		//
		// パーティクルと同じ時間軸で、StartDelay が来たものから鳴らす。
		// 「絵と音を1枚のアセットにまとめる」のが狙いなので、
		// 鳴らす側は再生を伝えるだけでよく、音を別に鳴らしに行かなくてよい。
		//
		// 単発音は1回鳴らすだけ。毎フレーム Play を呼ぶと頭出しが繰り返されて
		// 音が伸びないので、鳴らした印(soundTriggered)で1回に抑える
		//------------------------------------------------------------------
		if (a_pAudioManager)
		{
			// エディターで音を差し替えたぶんをここで拾う。
			// 食い違っているスロットだけ作り直すので、毎フレーム通してよい
			SyncSoundInstances(a_asset, *a_pAudioManager, a_inst);

			//----------------------------------------------------------
			// 鳴っている音の音量を、個体ごとのパラメータに追わせる
			// (ブーストの強さで噴射音を大きくする、など)。
			// 結び付けが無いアセットでは何もしない
			//----------------------------------------------------------
			if (a_asset.HasParamBinding(Resource::EEffectParamTarget::SoundVolume))
			{
				const size_t _volumeCount = std::min<size_t>(_soundParts.size(), Resource::EFFECT_SOUND_MAX);
				for (size_t _i = 0; _i < _volumeCount; ++_i)
				{
					if (!_soundParts[_i].IsValid() || !a_inst.soundTriggered[_i]) continue;
					if (auto* _pInstance = a_pAudioManager->RefInstance(a_inst.soundHandles[_i]))
					{
						_pInstance->SetVolume(_soundParts[_i].vol *
							a_asset.EvaluateParamScale(Resource::EEffectParamTarget::SoundVolume, _i, a_pParams));
					}
				}
			}

			const size_t _soundCount = std::min<size_t>(_soundParts.size(), Resource::EFFECT_SOUND_MAX);
			for (size_t _i = 0; _i < _soundCount; ++_i)
			{
				const Resource::EffectSoundPart& _part = _soundParts[_i];
				if (!_part.IsValid()) continue;

				// 今の状態で動く番でない音。
				// 止めている最中に再生し直されたとき、OnStop のループ音が残っていたらここで止めて返す
				float _elapsed = 0.0f;
				if (!IsPartTurn(_part.timing, a_inst, _elapsed))
				{
					if (_part.isLoop && a_inst.soundTriggered[_i])
					{
						ReturnVoice(*a_pAudioManager, a_inst, _i);
						a_inst.soundTriggered[_i] = false;
					}
					continue;
				}

				// まだ鳴らす時間になっていない
				if (_elapsed < _part.timing.startDelay) continue;

				// ループ音は Duration で止めて返す(0 なら止めるまで鳴りっぱなし)。
				// 単発音は自分で鳴り終わるので、長さの指定は見ない
				if (_part.isLoop && _part.timing.IsFinishedAt(_elapsed))
				{
					if (a_inst.soundTriggered[_i])
					{
						ReturnVoice(*a_pAudioManager, a_inst, _i);
					}
					continue;
				}

				if (a_inst.soundTriggered[_i]) continue;
				a_inst.soundTriggered[_i] = true;

				// 同じ音を鳴らしすぎていないか(エフェクトをまたいで数える)。
				// 間引かれたら鳴らさずに「鳴らした」扱いにする(後から遅れて鳴らない)
				if (!a_pAudioManager->CanPlaySound(
						_part.soundGUID,
						_part.minInterval,
						static_cast<uint32_t>((std::max)(_part.maxConcurrent, 0))))
				{
					continue;
				}

				// 鳴らす直前に声を借りる
				auto* _pInstance = BorrowVoice(*a_pAudioManager, a_inst, _i, _part);
				if (!_pInstance) continue;

				// 3D 指定でも、発行が2Dだったなら2Dで鳴らす
				// (3D かどうかは発行時にしか決められないため。CreateSoundInstances 参照)
				if (_part.is3DSound && _pInstance->Is3D())
				{
					_pInstance->SetCurveDistanceScaler(_part.distanceScaler);
					_pInstance->Play3D(a_inst.soundPos, _part.isLoop);
				}
				else
				{
					_pInstance->Play(_part.isLoop);
				}

				// Play3D は音量を 1 に戻すので、鳴らした後に入れ直す。
				// エディターで音量をいじったぶんと、個体ごとのパラメータの倍率もここで乗る
				_pInstance->SetVolume(_part.vol * a_asset.EvaluateParamScale(Resource::EEffectParamTarget::SoundVolume, _i, a_pParams));

				// 関所に知らせる(最後に鳴らした時刻と、鳴っている声)
				a_pAudioManager->NotifySoundPlayed(_part.soundGUID, a_inst.soundHandles[_i]);
			}
		}

		//------------------------------------------------------------------
		// 止めている最中の終わり
		//
		// OnStop のパーツが全部出し終わったら止まりきる。
		// 単発音は鳴りきらせ(止めない)、ループ音は Duration 0 なら止まりきるところで止める
		//------------------------------------------------------------------
		if (a_inst.isStopping && IsStopPartsDone(a_asset, a_inst))
		{
			a_inst.isStopping = false;

			if (a_pAudioManager)
			{
				const size_t _soundCount = std::min<size_t>(_soundParts.size(), Resource::EFFECT_SOUND_MAX);
				for (size_t _i = 0; _i < _soundCount; ++_i)
				{
					const Resource::EffectSoundPart& _part = _soundParts[_i];
					if (!_part.IsValid() || !_part.timing.IsStopTrigger() || !_part.isLoop) continue;
					if (!a_inst.soundTriggered[_i]) continue;

					ReturnVoice(*a_pAudioManager, a_inst, _i);
					a_inst.soundTriggered[_i] = false;
				}
			}
		}
	}

	bool EffectPlayer::IsStopPartsDone(const Resource::EffectAsset& a_asset, const EffectInstance& a_inst)
	{
		for (const auto& _part : a_asset.GetParticleParts())
		{
			if (!_part.IsValid() || !_part.timing.IsStopTrigger()) continue;
			if (!_part.timing.IsStopPartDoneAt(a_inst.stopElapsed)) return false;
		}
		for (const auto& _part : a_asset.GetMeshParts())
		{
			if (!_part.IsValid() || !_part.timing.IsStopTrigger()) continue;
			if (!_part.timing.IsStopPartDoneAt(a_inst.stopElapsed)) return false;
		}
		for (const auto& _part : a_asset.GetSoundParts())
		{
			if (!_part.IsValid() || !_part.timing.IsStopTrigger()) continue;

			// 単発音は鳴らし始めれば済み(鳴りきるのは止まりきった後でもよい)。
			// ループ音は Duration が過ぎるまで(0 なら鳴らし始めれば済み)
			if (!_part.timing.IsStopPartDoneAt(a_inst.stopElapsed)) return false;
		}
		return true;
	}

	bool EffectPlayer::IsFinished(const Resource::EffectAsset& a_asset, const EffectInstance& a_inst, Engine::Audio::AudioManager* a_pAudioManager)
	{
		const auto& _particleParts = a_asset.GetParticleParts();
		const auto& _meshParts = a_asset.GetMeshParts();
		const auto& _soundParts = a_asset.GetSoundParts();

		// まだ一度も再生していない/止めたものは「出し切った」ではない。
		// ここで true を返すと、再生前のエフェクトが
		// destroyOnFinish で湧いた瞬間に消える。
		// 止めている最中(OnStop のパーツを出している)も、まだ終わっていない
		if (!a_inst.isPlaying) return false;

		// 見るのは再生中に動くパーツ(OnPlay)だけ。
		// OnStop のパーツは止めるまで動かないので、出し切ったかの判定には入れない
		for (const auto& _part : _particleParts)
		{
			if (!_part.IsValid() || _part.timing.IsStopTrigger()) continue;
			if (!_part.timing.IsFinishedAt(a_inst.elapsed)) return false;
		}
		for (const auto& _part : _meshParts)
		{
			if (!_part.IsValid() || _part.timing.IsStopTrigger()) continue;
			if (!_part.timing.IsFinishedAt(a_inst.elapsed)) return false;
		}

		//------------------------------------------------------------------
		// サウンド
		//
		// 絵より音の方が長いことは珍しくない(爆発の余韻など)。
		// 音を見ないと、絵が消えたフレームで destroyOnFinish がエンティティごと
		// 消してしまい、借りている声も返却されて音がぶつ切りになる。
		//
		// 待つかどうかはパーツごとの isWaitFinish で選べる。
		// (BGM的に長い音を足したときに、エフェクトがいつまでも消えなくなるため)
		//------------------------------------------------------------------
		if (a_pAudioManager)
		{
			const size_t _soundCount = std::min<size_t>(_soundParts.size(), Resource::EFFECT_SOUND_MAX);
			for (size_t _i = 0; _i < _soundCount; ++_i)
			{
				const Resource::EffectSoundPart& _part = _soundParts[_i];
				if (!_part.IsValid() || _part.timing.IsStopTrigger()) continue;
				if (!_part.isWaitFinish) continue;

				// ループ音は止めるまで終わらない。パーティクルの出しっぱなしと同じ扱い
				if (_part.isLoop && _part.timing.duration <= 0.0f) return false;

				// 鳴らす前(待ち時間中)は、まだ終わっていない
				if (!a_inst.soundTriggered[_i])
				{
					if (a_inst.elapsed < _part.timing.startDelay) return false;
					continue;
				}

				auto* _pInstance = a_pAudioManager->RefInstance(a_inst.soundHandles[_i]);
				if (!_pInstance) continue;

				if (_pInstance->IsPlay()) return false;
			}
		}

		return true;
	}

	bool EffectPlayer::BuildMeshDraw(
		const Resource::EffectAsset& a_asset,
		size_t a_index,
		const EffectInstance& a_inst,
		const Math::Matrix& a_ownerWorld,
		Math::Matrix& a_outWorld,
		Math::Color& a_outColorScale,
		Math::Vector3& a_outEmissiveAdd)
	{
		const auto& _meshParts = a_asset.GetMeshParts();

		if (!a_inst.IsActive()) return false;
		if (a_index >= _meshParts.size()) return false;

		const Resource::EffectMeshPart& _part = _meshParts[a_index];
		if (!_part.IsValid()) return false;

		// 今の状態で動く番か(再生中は OnPlay、止めている最中は OnStop)と、その時間軸での経過時間
		float _elapsed = 0.0f;
		if (!IsPartTurn(_part.timing, a_inst, _elapsed)) return false;
		if (!_part.timing.IsActiveAt(_elapsed)) return false;

		// 出している区間の進み具合(0〜1)。出しっぱなしなら常に 0 = 開始時の見た目のまま
		const float _t = _part.timing.GetProgressAt(_elapsed);

		// ---- スケール : 開始値から終値へ寄せる ----
		const Math::Vector3 _scale =
		{
			std::lerp(_part.scale.x, _part.scale.x * _part.endScale.x, _t),
			std::lerp(_part.scale.y, _part.scale.y * _part.endScale.y, _t),
			std::lerp(_part.scale.z, _part.scale.z * _part.endScale.z, _t),
		};

		// ---- 配置 : 相手の行列基準のローカル配置を合成する ----
		// rotation は度で持っているのでラジアンへ直す。
		// CreateFromYawPitchRoll の並びは (yaw=Y, pitch=X, roll=Z)
		const Math::Matrix _local =
			Math::Matrix::CreateScale(_scale) *
			Math::Matrix::CreateFromYawPitchRoll(
				DirectX::XMConvertToRadians(_part.rotation.y),
				DirectX::XMConvertToRadians(_part.rotation.x),
				DirectX::XMConvertToRadians(_part.rotation.z)) *
			Math::Matrix::CreateTranslation(_part.posOffset);

		a_outWorld = _local * a_ownerWorld;

		// ---- 色 : アルファだけ終値へ寄せる ----
		a_outColorScale = _part.colorScale;
		a_outColorScale.a = std::lerp(_part.colorScale.a, _part.endAlpha, _t);

		// ---- 発光 : 強さを終値へ寄せて色に掛ける ----
		const float _intensity = std::lerp(_part.emissiveIntensity, _part.endEmissiveIntensity, _t);
		a_outEmissiveAdd =
		{
			_part.emissiveColor.x * _intensity,
			_part.emissiveColor.y * _intensity,
			_part.emissiveColor.z * _intensity,
		};

		return true;
	}

	bool EffectPlayer::BuildLightDraw(
		const Resource::EffectAsset& a_asset,
		size_t a_index,
		const EffectInstance& a_inst,
		const Math::Matrix& a_effectWorld,
		float a_rangeScale,
		Graphics::PointLight& a_outLight)
	{
		const auto& _lightParts = a_asset.GetLightParts();

		if (!a_inst.IsActive()) return false;
		if (a_index >= _lightParts.size()) return false;

		const Resource::EffectLightPart& _part = _lightParts[a_index];
		if (!_part.IsValid()) return false;

		// 今の状態で動く番か(再生中は OnPlay、止めている最中は OnStop)と、その時間軸での経過時間
		float _elapsed = 0.0f;
		if (!IsPartTurn(_part.timing, a_inst, _elapsed)) return false;
		if (!_part.timing.IsActiveAt(_elapsed)) return false;

		// 出している区間の進み具合(0〜1)。出しっぱなしなら常に 0 = 開始時の強さのまま
		const float _t = _part.timing.GetProgressAt(_elapsed);

		a_outLight.pos = Math::Vector3::Transform(_part.posOffset, a_effectWorld);
		a_outLight.color = _part.color;
		a_outLight.brightness = std::lerp(_part.brightness, _part.endBrightness, _t);
		a_outLight.range = _part.range * a_rangeScale;

		return true;
	}

	bool EffectPlayer::BuildTrailPoint(
		const Resource::EffectAsset& a_asset,
		size_t a_index,
		const EffectInstance& a_inst,
		const Math::Matrix& a_effectWorld,
		float a_widthScale,
		Math::Vector3& a_outPos,
		TrailStyle& a_outStyle)
	{
		const auto& _trailParts = a_asset.GetTrailParts();

		if (!a_inst.IsActive()) return false;
		if (a_index >= _trailParts.size()) return false;

		const Resource::EffectTrailPart& _part = _trailParts[a_index];
		if (!_part.IsValid()) return false;

		// 今の状態で動く番か(再生中は OnPlay、止めている最中は OnStop)と、その時間軸での経過時間
		float _elapsed = 0.0f;
		if (!IsPartTurn(_part.timing, a_inst, _elapsed)) return false;
		if (!_part.timing.IsActiveAt(_elapsed)) return false;

		a_outPos = Math::Vector3::Transform(_part.posOffset, a_effectWorld);

		a_outStyle.lifeTime = _part.lifeTime;
		a_outStyle.maxPoints = std::clamp(_part.maxPoints, 2, Resource::EFFECT_TRAIL_POINT_MAX);
		a_outStyle.minVertexDistance = _part.minVertexDistance * a_widthScale;
		a_outStyle.width = _part.width * a_widthScale;
		a_outStyle.endWidth = _part.endWidth * a_widthScale;
		a_outStyle.headColor = _part.headColor;
		a_outStyle.tailColor = _part.tailColor;
		a_outStyle.intensity = _part.intensity;
		a_outStyle.isAdditive = (_part.blend == Resource::EEffectTrailBlend::Additive);
		a_outStyle.textureHandle = _part.textureHandle;

		return true;
	}
}
