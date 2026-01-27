-- SPDX-License-Identifier: MIT

-- `r` is predefined in the environment and is the equivalent of rnn.init(<gpu>)


-- Should the given descriptor be shown decoded with the specified descriptor
-- type.  Some combinations can be ruled out, based on TYPE or FMT, for example.
-- If it is ambiguous, be conservative and show the specified decoding.
function show_descriptor(desc, type)

	if type == r.desctype.DESC_BUFFER then
		return r.a6xx_tex_type.A6XX_TEX_BUFFER == desc.TYPE
	end

	-- For any other descriptor type, TEX_BUFFER is invalid
	if r.a6xx_tex_type.A6XX_TEX_BUFFER == desc.TYPE then
		return false
	end

	local is_yuv = (desc.FMT == r.a6xx_format.FMT6_R8_G8B8_2PLANE_420_UNORM) or
		       (desc.FMT == r.a6xx_format.FMT6_R8_G8_B8_3PLANE_420_UNORM)

	if type == r.desctype.DESC_MULTI_PLANE then
		return is_yuv
	end

	-- For any other descriptor type, is_yuv is invalid
	if is_yuv then
		return false
	end

	-- we could be more precise with some info about the enabled
	-- shader stages, ie.
	--
	--   * if no img/img_bindless instructions, don't show DESC_WEIGHT
	--   * if no indirect sampler/texture indexing, we know which
	--     descriptors are samplers vs memobj
	--

	return true
end
