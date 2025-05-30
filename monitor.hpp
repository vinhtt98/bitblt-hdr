#pragma once
#include <tuple>
#include <dxgi1_6.h>
#include <d3d11.h>
#include "utils/com_ptr.hpp"

using vec2_t = std::tuple<int, int>;

class monitor
{
public:
	monitor(com_ptr<IDXGIOutput6> output, com_ptr<ID3D11Device> device);
	~monitor();

	std::string name();
	bool hdr_on() const;
	vec2_t virtual_position() const;
	float rotation() const;
	vec2_t resolution() const;
	float sdr_white_level() const;

	com_ptr<ID3D11Texture2D> take_screenshot();
	void update_output_desc();

private:
	void recreate_output_duplication();

	com_ptr<IDXGIOutput6> output_;
	com_ptr<IDXGIOutputDuplication> dup_;
	com_ptr<ID3D11Device> device_;
	com_ptr<ID3D11Texture2D> last_tex_;

	DXGI_OUTPUT_DESC1 desc_;

	std::string name_;
};
