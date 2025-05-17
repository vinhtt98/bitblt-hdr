#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wingdi.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <vector>

#include <MinHook.h>

#include "resource.h"

#ifndef _DEBUG
#define printf(...) ((void) 0)
#endif

namespace
{
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	IDXGIOutputDuplication* dup = nullptr;
	ID3D11ComputeShader* tonemap_cs = nullptr;
	IDXGIOutput6* target_output = nullptr;

	HINSTANCE self_instance;

	bool init_desktop_dup()
	{
		if (device && ctx && dup && tonemap_cs)
			return true;

		HRESULT hr;

		IDXGIFactory6* factory;
		hr = CreateDXGIFactory1(__uuidof(IDXGIFactory6), reinterpret_cast<void**>(&factory));

		if (FAILED(hr))
		{
			printf("init_desktop_dup failed at line %d, hr = 0x%x\n", __LINE__, hr);
			return false;
		}

		auto adapter_index = 0u;
		IDXGIAdapter1* target_adapter = nullptr;
		while (!target_adapter)
		{
			IDXGIAdapter4* adapter;
			hr = factory->EnumAdapters1(adapter_index++, reinterpret_cast<IDXGIAdapter1**>(&adapter));

			if (FAILED(hr))
			{
				printf("init_desktop_dup failed at line %d, hr = 0x%x\n", __LINE__, hr);

				factory->Release();
				return false;
			}

			auto outputIndex = 0u;
			while (true)
			{
				IDXGIOutput6* output;
				hr = adapter->EnumOutputs(outputIndex++, reinterpret_cast<IDXGIOutput**>(&output));

				if (FAILED(hr))
				{
					printf("init_desktop_dup failed at line %d, hr = 0x%x\n", __LINE__, hr);

					adapter->Release();
					factory->Release();
					return false;
				}

				DXGI_OUTPUT_DESC1 desc;
				hr = output->GetDesc1(&desc);

				if (FAILED(hr))
				{
					printf("init_desktop_dup failed at line %d, hr = 0x%x\n", __LINE__, hr);

					output->Release();
					adapter->Release();
					factory->Release();
					return false;
				}

				if (desc.AttachedToDesktop)
				{
					target_adapter = adapter;
					target_output = output;
					break;
				}

				output->Release();
			}

			if (target_adapter != adapter)
				adapter->Release();
		}

		factory->Release();

		D3D_FEATURE_LEVEL feature_level;

		hr = D3D11CreateDevice(
			target_adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
#ifdef _DEBUG
			D3D11_CREATE_DEVICE_DEBUG,
#else
			0,
#endif
			nullptr, 0, D3D11_SDK_VERSION,
			&device, &feature_level, &ctx
		);

		if (FAILED(hr))
		{
			printf("init_desktop_dup failed at line %d, hr = 0x%x\n", __LINE__, hr);

			target_output->Release();
			target_adapter->Release();
			return false;
		}

		if (device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0)
		{
			printf("init_desktop_dup failed at line %d, feature level < 11.0\n", __LINE__, hr);

			device->Release();
			device = nullptr;
			target_output->Release();
			target_adapter->Release();
			return false;
		}

		const DXGI_FORMAT formats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT };
		hr = target_output->DuplicateOutput1(device, 0, 1, formats, &dup);

		if (FAILED(hr))
		{
			printf("init_desktop_dup failed at line %d, hr = 0x%x\n", __LINE__, hr);

			ctx->Release();
			device->Release();

			ctx = nullptr;
			device = nullptr;

			target_output->Release();
			target_adapter->Release();

			return false;
		}

		target_adapter->Release();
		return true;
	}

	void compile_tonemapper_cs()
	{
		if (tonemap_cs)
		{
#if _DEBUG
			tonemap_cs->Release();
#else
			return;
#endif
		}

#if _DEBUG
		// compile tonemapping compute shader
		ID3DBlob* shader = nullptr;
		ID3DBlob* error = nullptr;

		HRESULT hr = D3DCompileFromFile(
			L"tonemapper.hlsl",
			nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			"main", "cs_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS, 0,
			&shader, &error
		);

		if (error)
		{
			printf("compile_tonemapper_cs shader compile error: %s\n", error->GetBufferPointer());
			error->Release();
		}

		if (FAILED(hr))
		{
			printf("compile_tonemapper_cs failed at line %d, hr = 0x%x\n", __LINE__, hr);

			if (shader)
				shader->Release();

			return;
		}

		hr = device->CreateComputeShader(
			shader->GetBufferPointer(), shader->GetBufferSize(),
			nullptr, &tonemap_cs
		);

		shader->Release();

		if (FAILED(hr))
		{
			printf("compile_tonemapper_cs failed at line %d, hr = 0x%x\n", __LINE__, hr);
		}
#else
		auto* const res = FindResourceA(self_instance, MAKEINTRESOURCE(TONEMAPPER_SHADER), RT_RCDATA);
		if (!res)
		{
			printf("compile_tonemapper_cs resource not found\n");
			return;
		}

		auto* const handle = LoadResource(self_instance, res);
		if (!handle)
		{
			printf("compile_tonemapper_cs failed to load resource\n");
			return;
		}

		const auto* bytecode = LockResource(handle);
		const auto size = SizeofResource(self_instance, res);

		HRESULT hr = device->CreateComputeShader(
			bytecode, size,
			nullptr, &tonemap_cs
		);

		FreeResource(handle);
		
		if (FAILED(hr))
		{
			printf("compile_tonemapper_cs failed at line %d, hr = 0x%x\n", __LINE__, hr);
		}
#endif
	}

	void free_desktop_dup()
	{
		if (tonemap_cs)
		{
			tonemap_cs->Release();
			tonemap_cs = nullptr;
		}

		if (dup)
		{
			dup->Release();
			dup = nullptr;
		}

		if (ctx)
		{
			ctx->Release();
			ctx = nullptr;
		}

		if (device)
		{
			device->Release();
			device = nullptr;
		}

		if (target_output)
		{
			target_output->Release();
			target_output = nullptr;
		}
	}

	ID3D11Texture2D* tonemap(ID3D11Texture2D* input)
	{
		// Do tonemapping with CS
		D3D11_TEXTURE2D_DESC desc;
		input->GetDesc(&desc);
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = 0;
		desc.CPUAccessFlags = 0;

		ID3D11Texture2D* tonemapped_tex = nullptr;
		HRESULT hr = device->CreateTexture2D(&desc, nullptr, &tonemapped_tex);
		if (FAILED(hr))
		{
			return nullptr;
		}

		ID3D11ShaderResourceView* src_srv;
		D3D11_SHADER_RESOURCE_VIEW_DESC src_desc = {};
		src_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		src_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		src_desc.Texture2D.MipLevels = 1;
		hr = device->CreateShaderResourceView(input, &src_desc, &src_srv);

		if (FAILED(hr))
		{
			tonemapped_tex->Release();
			return nullptr;
		}

		ID3D11UnorderedAccessView* dest_uav;
		D3D11_UNORDERED_ACCESS_VIEW_DESC dest_desc = {};
		dest_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		dest_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		dest_desc.Texture2D.MipSlice = 0;
		hr = device->CreateUnorderedAccessView(tonemapped_tex, &dest_desc, &dest_uav);

		if (FAILED(hr))
		{
			tonemapped_tex->Release();
			src_srv->Release();

			return nullptr;
		}

		ctx->CSSetShader(tonemap_cs, nullptr, 0);
		ctx->CSSetShaderResources(0, 1, &src_srv);
		ctx->CSSetUnorderedAccessViews(0, 1, &dest_uav, nullptr);

		ctx->Dispatch((desc.Width + 15) / 16, (desc.Height + 15) / 16, 1);

		ctx->CSSetShader(nullptr, nullptr, 0);
		ID3D11ShaderResourceView* null_srv = nullptr;
		ctx->CSSetShaderResources(0, 1, &null_srv);
		ID3D11UnorderedAccessView* null_uav = nullptr;
		ctx->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);

		src_srv->Release();
		dest_uav->Release();

		return tonemapped_tex;
	}

	void capture_frame(std::vector<uint8_t>& buffer, int& width, int& height)
	{
		compile_tonemapper_cs();

		DXGI_OUTDUPL_FRAME_INFO frame_info{ 0 };
		IDXGIResource* resource = nullptr;

		HRESULT hr = S_OK;

		while (!frame_info.LastPresentTime.QuadPart)
		{
			Sleep(0);
			dup->AcquireNextFrame(0, &frame_info, &resource);

			if (FAILED(hr)) return;

			if (!frame_info.LastPresentTime.QuadPart)
				dup->ReleaseFrame();
		}

		ID3D11Texture2D* tex = nullptr;
		hr = resource->QueryInterface(IID_PPV_ARGS(&tex));

		if (FAILED(hr))
		{
			resource->Release();
			dup->ReleaseFrame();
			return;
		}

		resource->Release();

		auto* tonemapped = tonemap(tex);

		tex->Release();

		D3D11_TEXTURE2D_DESC desc;
		tonemapped->GetDesc(&desc);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.MiscFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

		ID3D11Texture2D* staging_tex = nullptr;
		hr = device->CreateTexture2D(&desc, nullptr, &staging_tex);
		if (FAILED(hr))
		{
			tonemapped->Release();
			dup->ReleaseFrame();
			return;
		}

		ctx->CopyResource(staging_tex, tonemapped);
		tonemapped->Release();

		D3D11_MAPPED_SUBRESOURCE mapped;
		ctx->Map(staging_tex, 0, D3D11_MAP_READ, 0, &mapped);

		buffer.resize(desc.Width * desc.Height * 4);
		std::memcpy(buffer.data(), mapped.pData, buffer.size());

		width = desc.Width;
		height = desc.Height;

		ctx->Unmap(staging_tex, 0);
		staging_tex->Release();
		dup->ReleaseFrame();
	}

	void* BitBlt_Original = nullptr;
	BOOL WINAPI BitBltHook(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, DWORD rop)
	{
		printf("bitblt called\n");

		static bool inited = init_desktop_dup();

		if (!inited || (rop & CAPTUREBLT) == 0)
			return reinterpret_cast<decltype(BitBlt)*>(BitBlt_Original)(hdc, x, y, cx, cy, hdcSrc, x1, y1, rop);

		DXGI_OUTPUT_DESC1 desc;
		target_output->GetDesc1(&desc);

		if (desc.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
			return reinterpret_cast<decltype(BitBlt)*>(BitBlt_Original)(hdc, x, y, cx, cy, hdcSrc, x1, y1, rop);

		std::vector<uint8_t> buffer;
		int width, height;
		capture_frame(buffer, width, height);

		HBITMAP map = CreateBitmap(width, height, 1, 32, buffer.data());
		HDC src = CreateCompatibleDC(hdc);
		SelectObject(src, map);

		auto result = reinterpret_cast<decltype(BitBlt)*>(BitBlt_Original)(hdc, x, y, cx, cy, src, x1, y1, rop & ~CAPTUREBLT);

		DeleteDC(src);
		DeleteObject(map);

		return result;
	}

#if _DEBUG
	void create_console()
	{
		AllocConsole();

		FILE* f;
		auto _ = freopen_s(&f, "CONOUT$", "w+t", stdout);
		_ = freopen_s(&f, "CONOUT$", "w", stderr);
		_ = freopen_s(&f, "CONIN$", "r", stdin);
	}
#endif

	class hook_autoinit
	{
	public:
		hook_autoinit()
		{
#if _DEBUG
			create_console();
#endif

			LoadLibraryA("gdi32.dll");
			MH_Initialize();
			MH_CreateHookApi(L"gdi32.dll", "BitBlt", BitBltHook, &BitBlt_Original);
			MH_EnableHook(MH_ALL_HOOKS);
		}

		~hook_autoinit()
		{
			free_desktop_dup();
		}
	} hook;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD fdwReason, LPVOID)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		self_instance = instance;
	}

	return TRUE;
}
