#pragma once

//=============================================================================
// C/C++ Standard Library (STL)
//
// ここに置くものは多くの翻訳単位が使うものだけ。
// 数ファイルしか使わないヘッダーは、その使う側で読む
//=============================================================================
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <bitset>
#include <cmath>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <random>
#include <span>
#include <stack>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

//=============================================================================
// Windows API
//=============================================================================
#define NOMINMAX
#include <Windows.h>         // WinAPIの基本ヘッダー
#include <stdio.h>
#include <wrl/client.h>      // Microsoft::WRL::ComPtr（スマートポインタ）

template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

#pragma comment(lib, "Rpcrt4.lib") // UUID生成等
#pragma comment(lib, "winmm.lib")  // マルチメディアタイマー（FPS制御等）

//=============================================================================
// 外部ライブラリ (警告レベルを無効化してインクルード)
//
// 特定の経路でしか使わないライブラリはここへ置かない。
// リンクだけここで通し、ヘッダーは使う側で読む(各 #pragma comment の下に
// 読む場所を書いてある)
//=============================================================================
#pragma warning(push, 0)

//---------------------------------------------------------
// DirectX 12 Base
//---------------------------------------------------------
#include <dxgi1_6.h>         // スワップチェーンなどDXGI関連（DirectXの基盤）
#include <d3d12.h>           // D3D12のメインヘッダー

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib,"DirectXTK12.lib")

// CD3DX12_* のヘルパー(d3dx12.h)は使う側で読む。
// ヘッダーで必要なのはパイプラインステートストリームだけなので、
// PipelineStateManager.h は d3dx12_pipeline_state_stream.h を読む。
// DXGIのデバッグ機能(dxgidebug.h)は MainEngine.cpp だけ

//---------------------------------------------------------
// DirectX Shader Compiler
//
// ヘッダー(dxcapi.h / d3dcompiler.h / d3d12shader.h)は
// DXCCompiler.h・RasterizerImport.cpp などのコンパイル経路で読む
//---------------------------------------------------------
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxcompiler.lib")

//---------------------------------------------------------
// DirectX Math & Collision
//---------------------------------------------------------
#include <DirectXMath.h>       // 数学ライブラリ（ベクトル・行列）
#include <DirectXCollision.h>  // 当たり判定処理
#include <SimpleMath.h>        // 直感的な数学ライブラリ（DirectXTK）

namespace DXSM = DirectX::SimpleMath;

//---------------------------------------------------------
// DirectX Extensions (Mesh, Texture, Audio, Input)
//
// DirectXTex は Texture.cpp / TextureImporter.cpp、
// XInput は InputAxisForXInput.h / InputButtonForXInput.h で読む
//---------------------------------------------------------
#include <DirectXMesh.h>       // メッシュ処理
#include <Audio.h>             // サウンド処理

#pragma comment(lib, "DirectXMesh.lib")
#pragma comment(lib, "DirectXTex.lib")
#pragma comment(lib, "Xinput.lib")

//---------------------------------------------------------
// Other Third-Party Libraries
//---------------------------------------------------------
#include <magic_enum/magic_enum.hpp> // enumの拡張機能

#pragma warning(pop) // 外部ライブラリの警告無効化を解除

//=============================================================================
// 自作ヘッダーはここに置かない
//
// トップの PCH は STL / Windows / DirectX / 外部ライブラリなどの基礎だけ。
// 自作ヘッダーは分類ごとの PCH が自分の分類のものを足す。
//   Core/CorePCH.h       : Core
//   Engine/EnginePCH.h   : Engine(vcxproj の既定)
//   Application/AppPCH.h : App
//   Editor/EditorPCH.h   : Editor
// このファイルを直接使うのは、外部ライブラリの .cpp(imgui など)だけ
//=============================================================================
