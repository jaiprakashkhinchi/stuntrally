#include "pch.h"
#include "../Defines.h"

#include "WaterMaterial.h"
#include "MaterialDefinition.h"
#include "MaterialFactory.h"

#include <OgreMaterial.h>
#include <OgrePass.h>
#include <OgreTechnique.h>
#include <OgreHighLevelGpuProgramManager.h>
using namespace Ogre;

WaterMaterialGenerator::WaterMaterialGenerator()
{
	mName = "Water";
}

void WaterMaterialGenerator::generate(bool fixedFunction)
{
	mMaterial = prepareMaterial(mDef->getName());
	
	resetTexUnitCounter();
	
	// -------------------------- Main technique ----------------------------- //
	Ogre::Technique* technique = mMaterial->createTechnique();

	//  Main pass -----------------------------------------------------------
	Ogre::Pass* pass = technique->createPass();
	
	pass->setCullingMode(CULL_NONE);
	pass->setDepthWriteEnabled(false);
	pass->setSceneBlending(SBT_TRANSPARENT_ALPHA);
	
	Ogre::TextureUnitState* tu;
	//  normal map
	mNormalMap = pickTexture(&mDef->mProps->normalMaps);
	tu = pass->createTextureUnitState( mNormalMap );
	tu->setName("normalMap");
	mNormalTexUnit = mTexUnit_i; mTexUnit_i++;

	// env map
	tu = pass->createTextureUnitState( "white.png" );
	tu->setName("skyMap");
	tu->setTextureAddressingMode(TextureUnitState::TAM_MIRROR);
	mEnvTexUnit = mTexUnit_i; mTexUnit_i++;
	
	// shader
	if (!mShaderCached)
	{
		mVertexProgram = createVertexProgram();
		mFragmentProgram = createFragmentProgram();
	}
	
	pass->setVertexProgram(mVertexProgram->getName());
	pass->setFragmentProgram(mFragmentProgram->getName());
	
	if (mShaderCached)
	{
		individualFragmentProgramParams(pass->getFragmentProgramParameters());
		individualVertexProgramParams(pass->getFragmentProgramParameters());
	}

	// ----------------------------------------------------------------------- //
	
	createSSAOTechnique();
	
	// indicate we need 'time' parameter set every frame
	mParent->timeMtrs.push_back(mDef->getName());
	 
	// mParent->fogMtrs.... // not important, only for impostors
}

//----------------------------------------------------------------------------------------

void WaterMaterialGenerator::generateVertexProgramSource(Ogre::StringUtil::StrStreamType& outStream)
{
	outStream <<
	"struct VIn \n"
	"{ \n"
	"	float4 p : POSITION;	float3 n : NORMAL; \n"
	"	float3 t : TANGENT;		float3 uv: TEXCOORD0; \n"
	"}; \n"
	"struct VOut \n"
	"{ \n"
	"	float4 p : POSITION;	float3 uv : TEXCOORD0;	float4 wp : TEXCOORD1; \n"
	"	float3 n : TEXCOORD2;	float3 t  : TEXCOORD3;	float3 b  : TEXCOORD4; \n"
	"}; \n"
	"VOut main_vp(VIn IN, \n"
	"	uniform float4x4 wMat,  uniform float4x4 wvpMat, \n"
	"	uniform float4 fogParams) \n"
	"{ \n"
	"	VOut OUT;  OUT.uv = IN.uv; \n"
	"	OUT.wp = mul(wMat, IN.p); \n"
	"	OUT.p = mul(wvpMat, IN.p); \n"
	"	OUT.n = IN.n;  OUT.t = IN.t;  OUT.b = cross(IN.t, IN.n); \n"
	"	OUT.wp.w = saturate(fogParams.x * (OUT.p.z - fogParams.y) * fogParams.w); \n"
	"	return OUT; \n"
	"} \n";
}

//----------------------------------------------------------------------------------------

void WaterMaterialGenerator::vertexProgramParams(Ogre::HighLevelGpuProgramPtr program)
{
	GpuProgramParametersSharedPtr params = program->getDefaultParameters();

	params->setNamedAutoConstant("wMat", GpuProgramParameters::ACT_WORLD_MATRIX);
	params->setNamedAutoConstant("wvpMat", GpuProgramParameters::ACT_WORLDVIEWPROJ_MATRIX);
	params->setNamedAutoConstant("fogParams", GpuProgramParameters::ACT_FOG_PARAMS);
	
	individualVertexProgramParams(params);
}

//----------------------------------------------------------------------------------------

void WaterMaterialGenerator::individualVertexProgramParams(Ogre::GpuProgramParametersSharedPtr params)
{

}

//----------------------------------------------------------------------------------------

HighLevelGpuProgramPtr WaterMaterialGenerator::createVertexProgram()
{
	HighLevelGpuProgramManager& mgr = HighLevelGpuProgramManager::getSingleton();
	std::string progName = mDef->getName() + "_VP";

	HighLevelGpuProgramPtr ret = mgr.getByName(progName);
	if (!ret.isNull())
		mgr.remove(progName);

	ret = mgr.createProgram(progName, ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, 
		"cg", GPT_VERTEX_PROGRAM);

	if(MRTSupported())
	{
		ret->setParameter("profiles", "vs_4_0 vs_3_0 vp40");
	}
	else
	{
		ret->setParameter("profiles", "vs_4_0 vs_2_x arbvp1");
	}
	ret->setParameter("entry_point", "main_vp");

	StringUtil::StrStreamType outStream;
	generateVertexProgramSource(outStream);
	ret->setSource(outStream.str());
	ret->load();

	vertexProgramParams(ret);
	
	return ret;
}

//----------------------------------------------------------------------------------------

void WaterMaterialGenerator::generateFragmentProgramSource(Ogre::StringUtil::StrStreamType& outStream)
{
	//!todo global ambient?
	
	outStream <<
	"struct PIn \n"
	"{	float4 p : POSITION;	float3 uv : TEXCOORD0;	float4 wp : TEXCOORD1; \n"
	"	float3 n : TEXCOORD2;	float3 t  : TEXCOORD3;	float3 b  : TEXCOORD4; \n"
	"}; \n"
	"float4 main_fp(PIn IN,  \n"      ///  _water_
	"   uniform float3 lightSpec0, \n"
	"	uniform float4 matSpec,	\n"
	"	uniform float3 fogColor, \n"
	"	uniform float4 lightPos0,  uniform float3 camPos,	uniform float4x4 iTWMat, \n"
	"	uniform float2 waveBump, \n"
	"	uniform float waveHighFreq, uniform float waveSpecular, \n"
	"	uniform float time, \n"
	"	uniform float4 deepColor,  uniform float4 shallowColor,  uniform float4 reflectionColor, \n"
	"	uniform float2 reflAndWaterAmounts, \n"
	"	uniform float2 fresnelPowerBias, \n"
	"	uniform sampler2D normalMap : TEXUNIT0, \n"
	"	uniform sampler2D skyMap : TEXUNIT1): COLOR0 \n"
	"{ \n"
	"	float matShininess = matSpec.w; \n"
	"	float t = waveBump.y * time; \n"
		//  waves  sum 4 normal tex
	"	float2 uv1 = float2(-sin(-0.06f * t),cos(-0.06f * t)) * 0.5f; \n"  // slow big waves
	"	float2 uv2 = float2( cos( 0.062f* t),sin( 0.062f* t)) * 0.4f; \n"
	"	float2 uw1 = float2(-sin(-0.11f * t),cos(-0.11f * t)) * 0.23f; \n"  // fast small waves
	"	float2 uw2 = float2( cos( 0.112f* t),sin( 0.112f* t)) * 0.21f; \n"
	"	const float w1 = 0.25 + waveHighFreq, w2 = 0.25 - waveHighFreq; \n"
	"	float3 normalTex = ( \n"
	"		(tex2D(normalMap, IN.uv.xy*8 - uw2).rgb * 2.0 - 1.0) * w1 + \n"
	"		(tex2D(normalMap, IN.uv.xy*8 - uw1).rgb * 2.0 - 1.0) * w1 + \n"
	"		(tex2D(normalMap, IN.uv.xy*2 + uv1).rgb * 2.0 - 1.0) * w2 + \n"
	"		(tex2D(normalMap, IN.uv.xy*2 + uv2).rgb * 2.0 - 1.0) * w2); \n"
	"	normalTex = lerp(normalTex, float3(0,0,1), waveBump.x); \n"
		//  normal to object space
	"	float3x3 tbn = float3x3(IN.t, IN.b, IN.n); \n"
	"	float3 normal = mul(transpose(tbn), normalTex.xyz); \n"
	"	normal = normalize(mul((float3x3)iTWMat, normal)); \n"
		//  diffuse, specular
	"	float3 ldir = normalize(lightPos0.xyz - (lightPos0.w * IN.wp.xyz)); \n"
	"	float3 camDir = normalize(camPos - IN.wp.xyz); \n"
	"	float3 halfVec = normalize(ldir + camDir); \n"
	"	float3 specular = pow(max(dot(normal, halfVec), 0), matShininess); \n"
	"	float3 specC = specular * lightSpec0 * matSpec.rgb; \n"
	"	float3 clrSUM = /*diffC +*/ specC; \n"
		//  reflection  3D vec to sky dome map 2D uv
	"	float3 refl = reflect(-camDir, normal); \n"
	"	const float PI = 3.14159265358979323846; \n"
	"	float2 refl2; \n"
	"	refl2.x = /*(refl.x == 0) ? 0 :*/ ( (refl.z < 0.0) ? atan2(-refl.z, refl.x) : (2*PI - atan2(refl.z, refl.x)) ); \n"
	"	refl2.x = 1 - refl2.x / (2*PI); \n"  // yaw 0..1
	"	refl2.y = 1 - asin(refl.y) / PI*2;  //pitch 0..1 \n"
	"	float4 reflection = tex2D(skyMap, refl2) * reflectionColor; \n"
		//  fresnel
	"	float facing = 1.0 - max(abs(dot(camDir, normal)), 0); \n"
	"	float fresnel = saturate(fresnelPowerBias.y + pow(facing, fresnelPowerBias.x)); \n"
		//  water color
	"	float4 waterClr = lerp(shallowColor, deepColor, facing); \n"
	"	float4 reflClr = lerp(waterClr, reflection, fresnel); \n"
	"	float3 clr = clrSUM.rgb * waveSpecular + waterClr.rgb * reflAndWaterAmounts.y + reflClr.rgb * reflAndWaterAmounts.x; \n"
	"	clr = lerp(clr, fogColor, /*IN.fogVal*/IN.wp.w); \n"
	"	return float4(clr, waterClr.a + clrSUM.r); \n"
	"} \n";
}

//----------------------------------------------------------------------------------------

void WaterMaterialGenerator::fragmentProgramParams(Ogre::HighLevelGpuProgramPtr program)
{
	GpuProgramParametersSharedPtr params = program->getDefaultParameters();

	params->setNamedAutoConstant("iTWMat", GpuProgramParameters::ACT_INVERSE_TRANSPOSE_WORLD_MATRIX);
	params->setNamedAutoConstant("fogColor", GpuProgramParameters::ACT_FOG_COLOUR);
	params->setNamedAutoConstant("lightSpec0", GpuProgramParameters::ACT_LIGHT_SPECULAR_COLOUR, 0);
	params->setNamedAutoConstant("lightPos0", GpuProgramParameters::ACT_LIGHT_POSITION, 0);
	params->setNamedAutoConstant("camPos", GpuProgramParameters::ACT_CAMERA_POSITION);
	params->setNamedAutoConstant("time", GpuProgramParameters::ACT_TIME);
	
	individualFragmentProgramParams(params);
}

//----------------------------------------------------------------------------------------

void WaterMaterialGenerator::individualFragmentProgramParams(Ogre::GpuProgramParametersSharedPtr params)
{
	// can't set a vector2, need to pack it in vector3
	#define _vec3(prop) Vector3(prop.x, prop.y, 1.0)
	
	params->setNamedConstant("waveBump", _vec3(mDef->mProps->waveBump));
	params->setNamedConstant("waveHighFreq", Real(mDef->mProps->waveHighFreq));
	params->setNamedConstant("waveSpecular", Real(mDef->mProps->waveSpecular));
	params->setNamedConstant("deepColor", mDef->mProps->deepColour);
	params->setNamedConstant("shallowColor", mDef->mProps->shallowColour);
	params->setNamedConstant("reflectionColor", mDef->mProps->reflectionColour);
	params->setNamedConstant("matSpec", mDef->mProps->specular);
	params->setNamedConstant("reflAndWaterAmounts", Vector3(mDef->mProps->reflAmount, 1-mDef->mProps->reflAmount, 0));
	params->setNamedConstant("fresnelPowerBias", Vector3(mDef->mProps->fresnelPower, mDef->mProps->fresnelBias, 0));
}

//----------------------------------------------------------------------------------------

HighLevelGpuProgramPtr WaterMaterialGenerator::createFragmentProgram()
{
	HighLevelGpuProgramManager& mgr = HighLevelGpuProgramManager::getSingleton();
	std::string progName = mDef->getName() + "_FP";

	HighLevelGpuProgramPtr ret = mgr.getByName(progName);
	if (!ret.isNull())
		mgr.remove(progName);

	ret = mgr.createProgram(progName, ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, 
			"cg", GPT_FRAGMENT_PROGRAM);

	if(MRTSupported())
	{
		ret->setParameter("profiles", "ps_4_0 ps_3_0 fp40");
	}
	else
	{
		ret->setParameter("profiles", "ps_4_0 ps_2_x arbfp1");	
	}
	ret->setParameter("entry_point", "main_fp");

	StringUtil::StrStreamType outStream;
	generateFragmentProgramSource(outStream);
	ret->setSource(outStream.str());
	ret->load();

	GpuProgramParametersSharedPtr params = ret->getDefaultParameters();
	fragmentProgramParams(ret);
	
	return ret;
}
