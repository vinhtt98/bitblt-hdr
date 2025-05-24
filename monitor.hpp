#pragma once
#include <tuple>
#include <dxgi1_6.h>
#include <d3d11.h>

using vec2_t = std::tuple<int, int>;

class monitor
{
public:
	monitor(IDXGIOutput6* output, ID3D11Device* device);
	~monitor();

	std::string name();
	bool hdr_on();
	vec2_t virtual_position();
	vec2_t resolution();
	float sdr_white_level();

	ID3D11Texture2D* take_screenshot();
	void update_output_desc();

private:
	void recreate_output_duplication();

	IDXGIOutput6* output_;
	IDXGIOutputDuplication* dup_;
	ID3D11Device* device_;
	DXGI_OUTPUT_DESC1 desc_;
	ID3D11Texture2D* last_tex_;

	std::string name_;
};
