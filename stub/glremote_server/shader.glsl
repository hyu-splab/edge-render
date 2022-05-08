#version 330
#define GLES_OVER_GL

precision highp float;
precision highp int;

layout(location = 0) in highp vec2 vertex;

#ifdef USE_ATTRIB_LIGHT_ANGLE
layout(location = 2) in highp float light_angle;
#endif

/* clang-format on */
layout(location = 3) in vec4 color_attrib;

#ifdef USE_ATTRIB_MODULATE
layout(location = 5) in vec4 modulate_attrib; // attrib:5
#endif

// Usually, final_modulate is passed as a uniform. However during batching
// If larger fvfs are used, final_modulate is passed as an attribute.
// we need to read from the attribute in custom vertex shader
// rather than the uniform. We do this by specifying final_modulate_alias
// in shaders rather than final_modulate directly.
#ifdef USE_ATTRIB_MODULATE
#define final_modulate_alias modulate_attrib
#else
#define final_modulate_alias final_modulate
#endif

#ifdef USE_ATTRIB_LARGE_VERTEX
// shared with skeleton attributes, not used in batched shader
layout(location = 6) in vec2 translate_attrib; // attrib:6
layout(location = 7) in vec4 basis_attrib; // attrib:7
#endif

#ifdef USE_SKELETON
layout(location = 6) in uvec4 bone_indices; // attrib:6
layout(location = 7) in vec4 bone_weights; // attrib:7
#endif

#ifdef USE_TEXTURE_RECT

uniform vec4 dst_rect;
uniform vec4 src_rect;

#else

#ifdef USE_INSTANCING

layout(location = 8) in highp vec4 instance_xform0;
layout(location = 9) in highp vec4 instance_xform1;
layout(location = 10) in highp vec4 instance_xform2;
layout(location = 11) in lowp vec4 instance_color;

#ifdef USE_INSTANCE_CUSTOM
layout(location = 12) in highp vec4 instance_custom_data;
#endif

#endif

layout(location = 4) in highp vec2 uv_attrib;

// skeleton
#endif

uniform highp vec2 color_texpixel_size;

layout(std140) uniform CanvasItemData { //ubo:0

	highp mat4 projection_matrix;
	highp float time;
};

uniform highp mat4 modelview_matrix;
uniform highp mat4 extra_matrix;

out highp vec2 uv_interp;
out mediump vec4 color_interp;

#ifdef USE_ATTRIB_MODULATE
// modulate doesn't need interpolating but we need to send it to the fragment shader
flat out mediump vec4 modulate_interp;
#endif

#ifdef MODULATE_USED
uniform mediump vec4 final_modulate;
#endif

#ifdef USE_NINEPATCH

out highp vec2 pixel_size_interp;
#endif

#ifdef USE_SKELETON
uniform mediump sampler2D skeleton_texture; // texunit:-4
uniform highp mat4 skeleton_transform;
uniform highp mat4 skeleton_transform_inverse;
#endif

#ifdef USE_LIGHTING

layout(std140) uniform LightData { //ubo:1

	// light matrices
	highp mat4 light_matrix;
	highp mat4 light_local_matrix;
	highp mat4 shadow_matrix;
	highp vec4 light_color;
	highp vec4 light_shadow_color;
	highp vec2 light_pos;
	highp float shadowpixel_size;
	highp float shadow_gradient;
	highp float light_height;
	highp float light_outside_alpha;
	highp float shadow_distance_mult;
};

out vec4 light_uv_interp;
out vec2 transformed_light_uv;

out vec4 local_rot;

#ifdef USE_SHADOWS
out highp vec2 pos;
#endif

const bool at_light_pass = true;
#else
const bool at_light_pass = false;
#endif

#if defined(USE_MATERIAL)

/* clang-format off */
layout(std140) uniform UniformData { //ubo:2


};
/* clang-format on */

#endif

/* clang-format off */


/* clang-format on */

void main() {

	vec4 color = color_attrib;

#ifdef USE_INSTANCING
	mat4 extra_matrix_instance = extra_matrix * transpose(mat4(instance_xform0, instance_xform1, instance_xform2, vec4(0.0, 0.0, 0.0, 1.0)));
	color *= instance_color;

#ifdef USE_INSTANCE_CUSTOM
	vec4 instance_custom = instance_custom_data;
#else
	vec4 instance_custom = vec4(0.0);
#endif

#else
	mat4 extra_matrix_instance = extra_matrix;
	vec4 instance_custom = vec4(0.0);
#endif

#ifdef USE_TEXTURE_RECT

	if (dst_rect.z < 0.0) { // Transpose is encoded as negative dst_rect.z
		uv_interp = src_rect.xy + abs(src_rect.zw) * vertex.yx;
	} else {
		uv_interp = src_rect.xy + abs(src_rect.zw) * vertex;
	}
	highp vec4 outvec = vec4(dst_rect.xy + abs(dst_rect.zw) * mix(vertex, vec2(1.0, 1.0) - vertex, lessThan(src_rect.zw, vec2(0.0, 0.0))), 0.0, 1.0);

#else
	uv_interp = uv_attrib;
	highp vec4 outvec = vec4(vertex, 0.0, 1.0);
#endif

#ifdef USE_PARTICLES
	//scale by texture size
	outvec.xy /= color_texpixel_size;
#endif

#define extra_matrix extra_matrix_instance

	float point_size = 1.0;
	//for compatibility with the fragment shader we need to use uv here
	vec2 uv = uv_interp;
	{
		/* clang-format off */


		/* clang-format on */
	}

	gl_PointSize = point_size;
	uv_interp = uv;

#ifdef USE_NINEPATCH

	pixel_size_interp = abs(dst_rect.zw) * vertex;
#endif

#ifdef USE_ATTRIB_MODULATE
	// modulate doesn't need interpolating but we need to send it to the fragment shader
	modulate_interp = modulate_attrib;
#endif

#ifdef USE_ATTRIB_LARGE_VERTEX
	// transform is in attributes
	vec2 temp;

	temp = outvec.xy;
	temp.x = (outvec.x * basis_attrib.x) + (outvec.y * basis_attrib.z);
	temp.y = (outvec.x * basis_attrib.y) + (outvec.y * basis_attrib.w);

	temp += translate_attrib;
	outvec.xy = temp;

#else

	// transform is in uniforms
#if !defined(SKIP_TRANSFORM_USED)
	outvec = extra_matrix * outvec;
	outvec = modelview_matrix * outvec;
#endif

#endif // not large integer

#undef extra_matrix

	color_interp = color;

#ifdef USE_PIXEL_SNAP
	outvec.xy = floor(outvec + 0.5).xy;
	// precision issue on some hardware creates artifacts within texture
	// offset uv by a small amount to avoid
	uv_interp += 1e-5;
#endif

#ifdef USE_SKELETON

	if (bone_weights != vec4(0.0)) { //must be a valid bone
		//skeleton transform

		ivec4 bone_indicesi = ivec4(bone_indices);

		ivec2 tex_ofs = ivec2(bone_indicesi.x % 256, (bone_indicesi.x / 256) * 2);

		highp mat2x4 m;
		m = mat2x4(
					texelFetch(skeleton_texture, tex_ofs, 0),
					texelFetch(skeleton_texture, tex_ofs + ivec2(0, 1), 0)) *
			bone_weights.x;

		tex_ofs = ivec2(bone_indicesi.y % 256, (bone_indicesi.y / 256) * 2);

		m += mat2x4(
					 texelFetch(skeleton_texture, tex_ofs, 0),
					 texelFetch(skeleton_texture, tex_ofs + ivec2(0, 1), 0)) *
			 bone_weights.y;

		tex_ofs = ivec2(bone_indicesi.z % 256, (bone_indicesi.z / 256) * 2);

		m += mat2x4(
					 texelFetch(skeleton_texture, tex_ofs, 0),
					 texelFetch(skeleton_texture, tex_ofs + ivec2(0, 1), 0)) *
			 bone_weights.z;

		tex_ofs = ivec2(bone_indicesi.w % 256, (bone_indicesi.w / 256) * 2);

		m += mat2x4(
					 texelFetch(skeleton_texture, tex_ofs, 0),
					 texelFetch(skeleton_texture, tex_ofs + ivec2(0, 1), 0)) *
			 bone_weights.w;

		mat4 bone_matrix = skeleton_transform * transpose(mat4(m[0], m[1], vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0))) * skeleton_transform_inverse;

		outvec = bone_matrix * outvec;
	}

#endif

	gl_Position = projection_matrix * outvec;

#ifdef USE_LIGHTING

	light_uv_interp.xy = (light_matrix * outvec).xy;
	light_uv_interp.zw = (light_local_matrix * outvec).xy;

	mat3 inverse_light_matrix = mat3(inverse(light_matrix));
	inverse_light_matrix[0] = normalize(inverse_light_matrix[0]);
	inverse_light_matrix[1] = normalize(inverse_light_matrix[1]);
	inverse_light_matrix[2] = normalize(inverse_light_matrix[2]);
	transformed_light_uv = (inverse_light_matrix * vec3(light_uv_interp.zw, 0.0)).xy; //for normal mapping

#ifdef USE_SHADOWS
	pos = outvec.xy;
#endif

#ifdef USE_ATTRIB_LIGHT_ANGLE
	// we add a fixed offset because we are using the sign later,
	// and don't want floating point error around 0.0
	float la = abs(light_angle) - 1.0;

	// vector light angle
	vec4 vla;
	vla.xy = vec2(cos(la), sin(la));
	vla.zw = vec2(-vla.y, vla.x);
	vla.zw *= sign(light_angle);

	// apply the transform matrix.
	// The rotate will be encoded in the transform matrix for single rects,
	// and just the flips in the light angle.
	// For batching we will encode the rotation and the flips
	// in the light angle, and can use the same shader.
	local_rot.xy = normalize((modelview_matrix * (extra_matrix_instance * vec4(vla.xy, 0.0, 0.0))).xy);
	local_rot.zw = normalize((modelview_matrix * (extra_matrix_instance * vec4(vla.zw, 0.0, 0.0))).xy);
#else
	local_rot.xy = normalize((modelview_matrix * (extra_matrix_instance * vec4(1.0, 0.0, 0.0, 0.0))).xy);
	local_rot.zw = normalize((modelview_matrix * (extra_matrix_instance * vec4(0.0, 1.0, 0.0, 0.0))).xy);
#ifdef USE_TEXTURE_RECT
	local_rot.xy *= sign(src_rect.z);
	local_rot.zw *= sign(src_rect.w);
#endif
#endif // not using light angle

#endif
}

/* clang-format off */
