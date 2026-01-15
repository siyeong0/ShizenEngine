/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "pch.h"
#include <unordered_map>
#include <vector>
#include <memory>

#include "Platforms/Win64/Public/WinHPreface.h"
#include <D3Dcompiler.h>
#include <atlcomcli.h>
#include "Platforms/Win64/Public/WinHPostface.h"

#include "ShaderD3DBase.hpp"

#include <dxcapi.h>

#include "D3DErrors.hpp"
#include "Engine/Core/Memory/Public/DataBlobImpl.hpp"
#include "Engine/ShaderTools/Public/DXCompiler.hpp"
#include "Engine/ShaderTools/Public/HLSLUtils.hpp"
#include "Engine/Core/Common/Public/ThreadPool.hpp"

#ifndef D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES
#    define D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES (1 << 20)
#endif

namespace shz
{

	namespace
	{

		class D3DIncludeImpl : public ID3DInclude
		{
		public:
			D3DIncludeImpl(IShaderSourceInputStreamFactory* pStreamFactory)
				: m_pStreamFactory{ pStreamFactory }
			{
			}

			STDMETHOD(Open)
				(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes)
			{
				RefCntAutoPtr<IFileStream> pSourceStream;
				m_pStreamFactory->CreateInputStream(pFileName, &pSourceStream);
				if (pSourceStream == nullptr)
				{
					LOG_ERROR("Failed to open shader include file ", pFileName, ". Check that the file exists");
					return E_FAIL;
				}

				RefCntAutoPtr<DataBlobImpl> pFileData = DataBlobImpl::Create();
				pSourceStream->ReadBlob(pFileData);
				*ppData = pFileData->GetDataPtr();
				*pBytes = StaticCast<UINT>(pFileData->GetSize());

				m_DataBlobs.insert(std::make_pair(*ppData, pFileData));

				return S_OK;
			}

			STDMETHOD(Close)
				(LPCVOID pData)
			{
				m_DataBlobs.erase(pData);
				return S_OK;
			}

		private:
			IShaderSourceInputStreamFactory* m_pStreamFactory;
			std::unordered_map<LPCVOID, RefCntAutoPtr<IDataBlob>> m_DataBlobs;
		};

		HRESULT CompileShader(
			const char* Source,
			size_t SourceLength,
			const ShaderCreateInfo& ShaderCI,
			LPCSTR profile,
			ID3DBlob** ppBlobOut,
			ID3DBlob** ppCompilerOutput)
		{
			DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;

#if defined(SHZ_DEBUG)
			// Embed debug information in the shader bytecode.
			// Note: Debug info alone does NOT disable optimization.
			dwShaderFlags |= D3DCOMPILE_DEBUG;
			dwShaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;

			// Optional (often useful during debugging):
			// dwShaderFlags |= D3DCOMPILE_WARNINGS_ARE_ERRORS;
			// dwShaderFlags |= D3DCOMPILE_PREFER_FLOW_CONTROL;
#endif

			// Keep this updated when adding new engine-level flags.
			static_assert(SHADER_COMPILE_FLAG_LAST == (1u << 5u), "Did you add a new shader compile flag? You may need to handle it here.");

			// Engine-level flags -> FXC flags
			if (ShaderCI.CompileFlags & SHADER_COMPILE_FLAG_ENABLE_UNBOUNDED_ARRAYS)
				dwShaderFlags |= D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;

			if (ShaderCI.CompileFlags & SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR)
				dwShaderFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

			if (ShaderCI.CompileFlags & SHADER_COMPILE_FLAG_SKIP_OPTIMIZATION)
				dwShaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;

#if !defined(SHZ_DEBUG)
			// Release: keep default optimization behavior.
			// Avoid forcing OPTIMIZATION_LEVEL3 if it previously triggered compiler weirdness in your setup.
			// dwShaderFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

			D3D_SHADER_MACRO Macros[] = { {"D3DCOMPILER", ""}, {} };

			D3DIncludeImpl IncludeImpl{ ShaderCI.pShaderSourceStreamFactory };
			return D3DCompile(Source, SourceLength, nullptr, Macros, &IncludeImpl, ShaderCI.EntryPoint, profile, dwShaderFlags, 0, ppBlobOut, ppCompilerOutput);
		}
	}

	RefCntAutoPtr<IDataBlob> CompileD3DBytecode(
		const ShaderCreateInfo& ShaderCI,
		const ShaderVersion ShaderModel,
		IDXCompiler* DxCompiler,
		IDataBlob** ppCompilerOutput) noexcept(false)
	{
		if (ShaderCI.Source != nullptr || (ShaderCI.FilePath != nullptr && ShaderCI.SourceLanguage != SHADER_SOURCE_LANGUAGE_BYTECODE))
		{
			DEV_CHECK_ERR(ShaderCI.ByteCode == nullptr, "'ByteCode' must be null when shader is created from the source code or a file");
			DEV_CHECK_ERR(ShaderCI.EntryPoint != nullptr, "Entry point must not be null");

			bool UseDXC = false;

			// validate compiler type
			switch (ShaderCI.ShaderCompiler)
			{
			case SHADER_COMPILER_DEFAULT:
				UseDXC = false;
				break;

			case SHADER_COMPILER_DXC:
				UseDXC = DxCompiler != nullptr && DxCompiler->IsLoaded();
				if (!UseDXC)
					LOG_WARNING_MESSAGE("DXC compiler is not available. Using default shader compiler");
				break;

			case SHADER_COMPILER_FXC:
				UseDXC = false;
				break;

			default:
				LOG_ERROR_AND_THROW("Unsupported shader compiler");
			}

			if (UseDXC)
			{
				CComPtr<IDxcBlob> pShaderByteCode;
				DxCompiler->Compile(ShaderCI, ShaderModel, nullptr, &pShaderByteCode, nullptr, ppCompilerOutput);
				return DataBlobImpl::Create(pShaderByteCode->GetBufferSize(), pShaderByteCode->GetBufferPointer());
			}
			else
			{
				const String Profile = GetHLSLProfileString(ShaderCI.Desc.ShaderType, ShaderModel);
				const String HLSLSource = BuildHLSLSourceString(ShaderCI);

				CComPtr<ID3DBlob> CompilerOutput;
				CComPtr<ID3DBlob> pShaderByteCode;

				HRESULT hr = CompileShader(HLSLSource.c_str(), HLSLSource.length(), ShaderCI, Profile.c_str(), &pShaderByteCode, &CompilerOutput);
				HandleHLSLCompilerResult(SUCCEEDED(hr), CompilerOutput.p, HLSLSource, ShaderCI.Desc.Name, ppCompilerOutput);
				return DataBlobImpl::Create(pShaderByteCode->GetBufferSize(), pShaderByteCode->GetBufferPointer());
			}
		}
		else if (ShaderCI.ByteCode != nullptr)
		{
			DEV_CHECK_ERR(ShaderCI.ByteCodeSize != 0, "ByteCode size must be greater than 0");
			return DataBlobImpl::Create(ShaderCI.ByteCodeSize, ShaderCI.ByteCode);
		}
		else if (ShaderCI.FilePath != nullptr && ShaderCI.SourceLanguage == SHADER_SOURCE_LANGUAGE_BYTECODE)
		{
			if (ShaderCI.pShaderSourceStreamFactory == nullptr)
				LOG_ERROR_AND_THROW("Shader source stream factory must be provided when loading shader bytecode from a file");

			RefCntAutoPtr<IFileStream> pSourceStream;
			ShaderCI.pShaderSourceStreamFactory->CreateInputStream(ShaderCI.FilePath, &pSourceStream);
			if (!pSourceStream)
				LOG_ERROR_AND_THROW("Failed to load shader bytecode from file '", ShaderCI.FilePath, "'. Check that the file exists");

			RefCntAutoPtr<DataBlobImpl> pByteCode = DataBlobImpl::Create();
			pSourceStream->ReadBlob(pByteCode);
			return pByteCode;
		}
		else
		{
			LOG_ERROR_AND_THROW("Shader source must be provided through one of the 'Source', 'FilePath' or 'ByteCode' members");
		}

		return {};
	}

} // namespace shz
