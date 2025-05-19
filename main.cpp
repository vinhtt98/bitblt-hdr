#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wingdi.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <vector>
#include <format>

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

	struct tonemap_constant_buffer_t
	{
		float white_level = 200.0f;
		float reserved[3];
	} tonemap_cb_data;

	ID3D11Buffer* tonemap_const_buffer = nullptr;

	HINSTANCE self_instance;

	bool recreate_desktop_duplication_api()
	{
		if (dup)
		{
			dup->Release();
			dup = nullptr;
		}

		const DXGI_FORMAT formats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT };
		auto hr = target_output->DuplicateOutput1(device, 0, 1, formats, &dup);

		if (FAILED(hr))
		{
			auto msg = std::format("recreate_desktop_duplication_api failed at line {}: {:X}", static_cast<unsigned long>(hr));
			MessageBoxA(nullptr, msg.data(), "Fatal", MB_OK | MB_ICONERROR);

			return false;
		}

		return true;
	}

	// https://chromium.googlesource.com/chromium/src/+/c71f15ab1ace78c7efeeeda9f8552b4af9db2877/ui/display/win/screen_win.cc#112
	bool get_path_info(HMONITOR monitor, DISPLAYCONFIG_PATH_INFO* path_info)
	{
		LONG result;
		uint32_t num_path_array_elements = 0;
		uint32_t num_mode_info_array_elements = 0;
		std::vector<DISPLAYCONFIG_PATH_INFO> path_infos;
		std::vector<DISPLAYCONFIG_MODE_INFO> mode_infos;

		// Get the monitor name.
		MONITORINFOEXW view_info;
		view_info.cbSize = sizeof(view_info);

		if (!GetMonitorInfoW(monitor, &view_info))
			return false;

		// Get all path infos.
		do
		{
			if (GetDisplayConfigBufferSizes(
				QDC_ONLY_ACTIVE_PATHS, &num_path_array_elements,
				&num_mode_info_array_elements) != ERROR_SUCCESS)
			{
				return false;
			}
			path_infos.resize(num_path_array_elements);
			mode_infos.resize(num_mode_info_array_elements);
			result = QueryDisplayConfig(
				QDC_ONLY_ACTIVE_PATHS, &num_path_array_elements, path_infos.data(),
				&num_mode_info_array_elements, mode_infos.data(), nullptr);
		} while (result == ERROR_INSUFFICIENT_BUFFER);

		// Iterate of the path infos and see if we find one with a matching name.
		if (result == ERROR_SUCCESS)
		{
			for (uint32_t p = 0; p < num_path_array_elements; p++)
			{
				DISPLAYCONFIG_SOURCE_DEVICE_NAME device_name;
				device_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
				device_name.header.size = sizeof(device_name);
				device_name.header.adapterId = path_infos[p].sourceInfo.adapterId;
				device_name.header.id = path_infos[p].sourceInfo.id;
				if (DisplayConfigGetDeviceInfo(&device_name.header) == ERROR_SUCCESS)
				{
					if (wcscmp(view_info.szDevice, device_name.viewGdiDeviceName) == 0)
					{
						*path_info = path_infos[p];
						return true;
					}
				}
			}
		}

		return false;
	}

	float get_sdr_white_level(HMONITOR monitor)
	{
		const float default_white_level = 200.0f;

		DISPLAYCONFIG_PATH_INFO path_info = {};
		if (!get_path_info(monitor, &path_info))
			return default_white_level; // default

		DISPLAYCONFIG_SDR_WHITE_LEVEL white_level = {};
		white_level.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
		white_level.header.size = sizeof(white_level);
		white_level.header.adapterId = path_info.targetInfo.adapterId;
		white_level.header.id = path_info.targetInfo.id;
		if (DisplayConfigGetDeviceInfo(&white_level.header) != ERROR_SUCCESS)
			return default_white_level;

		return white_level.SDRWhiteLevel * 80.0f / 1000.0f;
	}

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
			printf("init_desktop_dup failed at line %d, feature level < 11.0\n", __LINE__);

			device->Release();
			device = nullptr;
			target_output->Release();
			target_adapter->Release();
			return false;
		}

		target_adapter->Release();

		return true;
	}

	bool compile_tonemapper_cs()
	{
		if (tonemap_cs)
		{
#if _DEBUG
			tonemap_cs->Release();
			tonemap_cs = nullptr;
#else
			return true;
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
			printf("compile_tonemapper_cs shader compile: %s\n", reinterpret_cast<const char*>(error->GetBufferPointer()));
			error->Release();
		}

		if (FAILED(hr))
		{
			printf("compile_tonemapper_cs failed at line %d, hr = 0x%x\n", __LINE__, hr);

			if (shader)
				shader->Release();

			return false;
		}

		hr = device->CreateComputeShader(
			shader->GetBufferPointer(), shader->GetBufferSize(),
			nullptr, &tonemap_cs
		);

		shader->Release();

		if (FAILED(hr))
		{
			printf("compile_tonemapper_cs failed at line %d, hr = 0x%x\n", __LINE__, hr);
			return false;
		}
#else
		auto* const res = FindResourceA(self_instance, MAKEINTRESOURCE(TONEMAPPER_SHADER), RT_RCDATA);
		if (!res)
		{
			printf("compile_tonemapper_cs resource not found\n");
			return false;
		}

		auto* const handle = LoadResource(self_instance, res);
		if (!handle)
		{
			printf("compile_tonemapper_cs failed to load resource\n");
			return false;
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
			return false;
		}
#endif

		return true;
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

		if (!tonemap_const_buffer)
		{
			D3D11_BUFFER_DESC cb_desc;
			cb_desc.ByteWidth = sizeof(tonemap_constant_buffer_t);
			cb_desc.Usage = D3D11_USAGE_DYNAMIC;
			cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			cb_desc.MiscFlags = 0;
			cb_desc.StructureByteStride = 0;

			hr = device->CreateBuffer(&cb_desc, nullptr, &tonemap_const_buffer);

			if (FAILED(hr))
			{
				tonemapped_tex->Release();
				src_srv->Release();
				dest_uav->Release();

				return nullptr;
			}

			ctx->CSSetConstantBuffers(0, 1, &tonemap_const_buffer);
		}

		D3D11_MAPPED_SUBRESOURCE mapped_cb;
		ctx->Map(tonemap_const_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cb);
		memcpy(mapped_cb.pData, &tonemap_cb_data, sizeof(tonemap_constant_buffer_t));
		ctx->Unmap(tonemap_const_buffer, 0);

		ctx->CSSetShader(tonemap_cs, nullptr, 0);
		ctx->CSSetShaderResources(0, 1, &src_srv);
		ctx->CSSetUnorderedAccessViews(0, 1, &dest_uav, nullptr);
		ctx->Dispatch((desc.Width + 15) / 16, (desc.Height + 15) / 16, 1);

		ctx->CSSetShader(nullptr, nullptr, 0);

		src_srv->Release();
		src_srv = nullptr;
		ctx->CSSetShaderResources(0, 1, &src_srv);

		dest_uav->Release();
		dest_uav = nullptr;
		ctx->CSSetUnorderedAccessViews(0, 1, &dest_uav, nullptr);

		return tonemapped_tex;
	}

	void capture_frame(std::vector<uint8_t>& buffer, int& width, int& height)
	{
		if (!compile_tonemapper_cs())
			return;

		if (!dup) recreate_desktop_duplication_api();

		DXGI_OUTDUPL_FRAME_INFO frame_info{ 0 };
		IDXGIResource* resource = nullptr;

		HRESULT hr = S_OK;

		while (!frame_info.LastPresentTime.QuadPart)
		{
			Sleep(0);
			hr = dup->AcquireNextFrame(0, &frame_info, &resource);

			if (hr == DXGI_ERROR_ACCESS_LOST)
			{
				auto result = recreate_desktop_duplication_api();
				if (!result) return;

				continue;
			}

			if (hr == DXGI_ERROR_WAIT_TIMEOUT)
			{
				Sleep(20);
				continue;
			}

			if (FAILED(hr))
			{
				auto msg = std::format("failed to acquire next frame: {:x}", hr);
				MessageBoxA(nullptr, msg.data(), "Fatal", MB_OK | MB_ICONERROR);
				return;
			}

			if (!frame_info.LastPresentTime.QuadPart)
			{
				hr = dup->ReleaseFrame();

				if (FAILED(hr)) return;
			}
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

		tonemap_cb_data.white_level = get_sdr_white_level(desc.Monitor);
		printf("white level: %f\n", tonemap_cb_data.white_level);

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
