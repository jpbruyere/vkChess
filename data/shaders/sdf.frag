#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 1) uniform sampler2D samplerColor;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

const vec3 textColor = vec3(0.2,0.2,0.8);
const vec3 outlineColor = vec3(0.9,0.9,0.9);
const float outlineWidth = 0.0;

void main()
{
	float dist = texture(samplerColor, inUV).a;
	float smoothWidth = fwidth(dist);
	float alpha = smoothstep(0.5 - smoothWidth, 0.5 + smoothWidth, dist);
	if (alpha < 0.001)
		discard;

	vec3 rgb = vec3(alpha);

	/*if (outlineWidth > 0.0)
	{
		float w = 1.0 - outlineWidth;
		alpha = smoothstep(w - smoothWidth, w + smoothWidth, distance);
		rgb += mix(vec3(alpha), outlineColor.rgb, alpha);
	}*/

	outFragColor = vec4(rgb, alpha);

}
