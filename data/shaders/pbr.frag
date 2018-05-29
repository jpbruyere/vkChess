#version 450

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) flat in int inMatIdx;

struct Material {
	vec4 baseColorFactor;
	uint alphaMode;
	float alphaCutoff;
	float metallicFactor;
	float roughnessFactor;

	uint baseColorTexture;
	uint metallicRoughnessTexture;
	uint normalTexture;
	uint occlusionTexture;

	uint emissiveTexture;
	uint pad1;
	uint pad2;
	uint pad3;
};
// Scene bindings

layout (set = 0, binding = 0) uniform UBO {
	mat4 projection;
	mat4 model;
	mat4 view;
	vec3 camPos;
} ubo;

layout (set = 0, binding = 1) uniform UBOParams {
	vec4 lightDir;
	float exposure;
	float gamma;
	float prefilteredCubeMipLevels;
} uboParams;

layout (set = 0, binding = 2) uniform samplerCube samplerIrradiance;
layout (set = 0, binding = 3) uniform samplerCube prefilteredMap;
layout (set = 0, binding = 4) uniform sampler2D samplerBRDFLUT;

// Material bindings

layout (set = 1, binding = 0) uniform sampler2DArray maps;
layout (set = 1, binding = 1) uniform UBOMaterial
{
	Material mats[16];
} uboMat;

layout (location = 0) out vec4 outColor;

#define PI 3.1415926535897932384626433832795
#define ALBEDO pow(texture(albedoMap, vec3(inUV, uboMat.mats[inMatIdx].baseColorTexture - 1).rgb, vec3(2.2))

// From http://filmicgames.com/archives/75
vec3 Uncharted2Tonemap(vec3 x)
{
	float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

// Normal Distribution function --------------------------------------
float D_GGX(float dotNH, float roughness)
{
	float alpha = roughness * roughness;
	float alpha2 = alpha * alpha;
	float denom = dotNH * dotNH * (alpha2 - 1.0) + 1.0;
	return (alpha2)/(PI * denom*denom);
}

// Geometric Shadowing function --------------------------------------
float G_SchlicksmithGGX(float dotNL, float dotNV, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r*r) / 8.0;
	float GL = dotNL / (dotNL * (1.0 - k) + k);
	float GV = dotNV / (dotNV * (1.0 - k) + k);
	return GL * GV;
}

// Fresnel function ----------------------------------------------------
vec3 F_Schlick(float cosTheta, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
vec3 F_SchlickR(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 prefilteredReflection(vec3 R, float roughness)
{
	float lod = roughness * uboParams.prefilteredCubeMipLevels;
	float lodf = floor(lod);
	float lodc = ceil(lod);
	vec3 a = textureLod(prefilteredMap, R, lodf).rgb;
	vec3 b = textureLod(prefilteredMap, R, lodc).rgb;
	return mix(a, b, lod - lodf);
}

vec3 specularContribution(vec3 L, vec3 V, vec3 N, vec3 F0, float metallic, float roughness, vec3 albedo)
{
	// Precalculate vectors and dot products
	vec3 H = normalize (V + L);
	float dotNH = clamp(dot(N, H), 0.0, 1.0);
	float dotNV = clamp(dot(N, V), 0.0, 1.0);
	float dotNL = clamp(dot(N, L), 0.0, 1.0);

	// Light color fixed
	vec3 lightColor = vec3(1.0);

	vec3 color = vec3(0.0);

	if (dotNL > 0.0) {
		// D = Normal distribution (Distribution of the microfacets)
		float D = D_GGX(dotNH, roughness);
		// G = Geometric shadowing term (Microfacets shadowing)
		float G = G_SchlicksmithGGX(dotNL, dotNV, roughness);
		// F = Fresnel factor (Reflectance depending on angle of incidence)
		vec3 F = F_Schlick(dotNV, F0);
		vec3 spec = D * F * G / (4.0 * dotNL * dotNV + 0.001);
		vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
		color += (kD * albedo / PI + spec) * dotNL;
	}

	return color;
}

// See http://www.thetenthplanet.de/archives/1180
vec3 perturbNormal()
{
	vec3 tangentNormal = texture(maps, vec3(inUV, uboMat.mats[inMatIdx].normalTexture - 1)).xyz * 2.0 - 1.0;

	vec3 q1 = dFdx(inWorldPos);
	vec3 q2 = dFdy(inWorldPos);
	vec2 st1 = dFdx(inUV);
	vec2 st2 = dFdy(inUV);

	vec3 N = normalize(inNormal);
	vec3 T = normalize(q1 * st2.t - q2 * st1.t);
	vec3 B = -normalize(cross(N, T));
	mat3 TBN = mat3(T, B, N);

	return normalize(TBN * tangentNormal);
}

void main()
{
	vec4 baseColor = uboMat.mats[inMatIdx].baseColorFactor;
	if (uboMat.mats[inMatIdx].baseColorTexture > 0)
		baseColor *= texture(maps, vec3(inUV, uboMat.mats[inMatIdx].baseColorTexture-1));

	if (uboMat.mats[inMatIdx].alphaMode == 1) {
		if (baseColor.a < uboMat.mats[inMatIdx].alphaCutoff) {
			discard;
		}
	}

	vec3 N = (uboMat.mats[inMatIdx].normalTexture > 0) ? perturbNormal() : normalize(inNormal);
	vec3 V = normalize(ubo.camPos - inWorldPos);
	vec3 R = -normalize(reflect(V, N));

	float metallic = uboMat.mats[inMatIdx].metallicFactor;
	float roughness = uboMat.mats[inMatIdx].roughnessFactor;
	if (uboMat.mats[inMatIdx].metallicRoughnessTexture > 0) {
		metallic *= texture(maps, vec3(inUV, uboMat.mats[inMatIdx].metallicRoughnessTexture - 1)).b;
		roughness *= clamp(texture(maps, vec3(inUV, uboMat.mats[inMatIdx].metallicRoughnessTexture - 1)).g, 0.04, 1.0);
	}

	vec3 Cdiff = mix (baseColor.rgb * 0.96, vec3(0.0), metallic);
	vec3 F0 = vec3(0.04);
	F0 = mix(F0, baseColor.rgb, metallic);
	float alpha = pow(roughness, 2.0);

	vec3 L = normalize(uboParams.lightDir.xyz);
	vec3 Lo = specularContribution(L, V, N, F0, metallic, roughness, baseColor.rgb);

	vec2 brdf = texture(samplerBRDFLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;

	vec3 reflection = prefilteredReflection(R, roughness).rgb;
	vec3 irradiance = texture(samplerIrradiance, N).rgb;

	// Diffuse based on irradiance
	vec3 diffuse = irradiance * baseColor.rgb;

	vec3 F = F_SchlickR(max(dot(N, V), 0.0), F0, roughness);

	// Specular reflectance
	vec3 specular = reflection * (F * brdf.x + brdf.y);

	// Ambient part
	vec3 kD = 1.0 - F;
	kD *= 1.0 - metallic;
	float ao = (uboMat.mats[inMatIdx].occlusionTexture > .0f) ? texture(maps, vec3(inUV, uboMat.mats[inMatIdx].occlusionTexture - 1)).r : 1.0f;
	vec3 ambient = (kD * diffuse + specular) * ao;
	vec3 color = ambient + Lo;

	// Tone mapping
	color = Uncharted2Tonemap(color * uboParams.exposure);
	color = color * (1.0f / Uncharted2Tonemap(vec3(11.2f)));
	// Gamma correction
	color = pow(color, vec3(1.0f / uboParams.gamma));

	if (uboMat.mats[inMatIdx].emissiveTexture > .0f) {
		vec3 emissive = texture(maps, vec3(inUV, uboMat.mats[inMatIdx].emissiveTexture - 1)).rgb;// * u_EmissiveFactor;
		color += emissive;
	}

	outColor = vec4(color, baseColor.a);
}
